#include "adc.h"

#include <FreeRtos.h>
#include <portmacro.h>
#include <queue.h>

#include <cstring>

#include "controller.h"
#include "dma.h"
#include "host_link.h"
#include "io.h"
#include "logger.h"
#include "main.h"
#include "serial_packets_client.h"
#include "spi.h"
#include "static_queue.h"
#include "stm32h7xx_hal_spi.h"
#include "stm32h7xx_hal_spi_ex.h"
#include "tim.h"
#include "time_util.h"

extern DMA_HandleTypeDef hdma_spi1_tx;
// extern DMA_HandleTypeDef hdma_spi1_rx;

using host_link::HostPorts;

// enum TransferMode {
//   SINGLE_COMMAND,
//   CONTINIOUS_READING
// };

namespace adc {

// We use double buffering using cyclic DMA transfer over two buffers.
constexpr uint32_t kBytesPerPoint = 3;
constexpr uint32_t kPointsPerGroup = 100;
constexpr uint32_t kBytesPerGroup = kBytesPerPoint * kPointsPerGroup;

// Each of tx/rx buffer contains two groups (halves) for cyclic DMA transfer
static uint8_t tx_buffer[2 * kBytesPerGroup] = {};
static uint8_t rx_buffer[2 * kBytesPerGroup] = {};

static SerialPacketsData packet_data;

// static uint8_t queue_items_static_mem[5] = {};
// static StaticQueue_t queue_static_mem;
// static QueueHandle_t queue_handle;
enum IrqEventId {
  EVENT_HALF_COMPLETE = 1,
  EVENT_FULL_COMPLETE = 2,
};

struct IrqEvent {
  IrqEventId id;
  uint32_t isr_millis;
};

static StaticQueue<IrqEvent, 5> irq_event_queue;

// Indicates if we transfer a one time command or
// we are in a continious cyclic transfer.
static volatile bool continious_DMA;

// static bool dma_active = false;
static volatile uint32_t irq_half_count = 0;
static volatile uint32_t irq_full_count = 0;
static volatile uint32_t irq_error_count = 0;

static volatile uint32_t event_half_count = 0;
static volatile uint32_t event_full_count = 0;

// static bool dma_one_shot = false;


static void trap() {
  // Breakpoint here.
  asm("nop");
}

// Called when the first half of rx_buffer is ready for processing.
void spi_TxRxHalfCpltCallback(SPI_HandleTypeDef *hspi) {
  trap();

  // In one time transfer we ignore the half complete since
  // we want to transfer the entire buffer before we stop
  // the DMA.
  if (continious_DMA) {
    irq_half_count++;
    BaseType_t task_woken;
    IrqEvent event = {.id = EVENT_HALF_COMPLETE,
                      .isr_millis = time_util::millis_from_isr()};
    irq_event_queue.add_from_isr(event, &task_woken);
    portYIELD_FROM_ISR(task_woken)
  }
}

// Called when the second half of rx_buffer is ready for processing.
void spi_TxRxCpltCallback(SPI_HandleTypeDef *hspi) {
  trap();

  // If trasfering a one time transaction, we shut off the
  // DMA transfer once the first transfer is completed.
  if (!continious_DMA) {
    HAL_SPI_Abort(&hspi1);
  } else {
    irq_full_count++;
  }

  BaseType_t task_woken;
  IrqEvent event = {.id = EVENT_FULL_COMPLETE,
                    .isr_millis = time_util::millis_from_isr()};
  irq_event_queue.add_from_isr(event, &task_woken);
  portYIELD_FROM_ISR(task_woken)
}

void spi_ErrorCallback(SPI_HandleTypeDef *hspi) {
  irq_error_count++;
  trap();
}


// Set up the DMA MUX request generator which controls how many
// transfers (bytes in out case) are done on each TIM12 PWM
// sync which we use as a repeating ADC CS.
static void set_dma_request_generator(uint32_t num_transfers_per_sync) {

  // Set tx DMA request generator.


  // A woraround to reset the request generator. 
  CLEAR_BIT(hdma_spi1_tx.DMAmuxChannel->CCR, (DMAMUX_CxCR_SE));


  const uint32_t mask = DMAMUX_CxCR_NBREQ_Msk;
  MODIFY_REG(hdma_spi1_tx.DMAmuxChannel->CCR, mask,
             (num_transfers_per_sync - 1U) << DMAMUX_CxCR_NBREQ_Pos);


  SET_BIT(hdma_spi1_tx.DMAmuxChannel->CCR, (DMAMUX_CxCR_SE));

}



// Blocks until completion. Recieved bytes are returned in rx_buffer.
static void send_command(const uint8_t *cmd, uint16_t num_bytes) {
  time_util::delay_millis(50);
  logger.info("Sending command (%hu bytes)", num_bytes);
  if (num_bytes > sizeof(rx_buffer)) {
    Error_Handler();
  }
  memset(rx_buffer, 0, num_bytes);

  // irq_event_queue.reset();

  set_dma_request_generator(num_bytes);
  continious_DMA = false;
  irq_event_queue.reset();

  const HAL_StatusTypeDef status =
      HAL_SPI_TransmitReceive_DMA(&hspi1, cmd, rx_buffer, num_bytes);
  if (status != HAL_StatusTypeDef::HAL_OK) {
    Error_Handler();
  }



  IrqEvent event;
  if (!irq_event_queue.consume_from_task(&event, 300)) {
    Error_Handler();
  }

  logger.info("IRQ event: %d", event.id);
  if (event.id != IrqEventId::EVENT_FULL_COMPLETE) {
    Error_Handler();
  }


}

void cmd_reset() {
  static const uint8_t cmd[] = {0x06};
  send_command(cmd, sizeof(cmd));
  // Since commands are done at a TIM12 intervals, we don't
  // need to insert a ~50us delay here as called by the datasheet.
}

void cmd_start() {
  static const uint8_t cmd[] = {0x08};
  send_command(cmd, sizeof(cmd));
}

struct AdcRegs {
  uint8_t r0;
  uint8_t r1;
  uint8_t r2;
  uint8_t r3;
};

void cmd_read_registers(AdcRegs *regs) {
  static const uint8_t cmd[] = {0x23, 0, 0, 0, 0};
  // Initializaing with a visible sentinel.
  // memset(rx_buffer, 0x99, sizeof(rx_buffer));
  send_command(cmd, sizeof(cmd));
  regs->r0 = rx_buffer[1];
  regs->r1 = rx_buffer[2];
  regs->r2 = rx_buffer[3];
  regs->r3 = rx_buffer[4];
}

static void cmd_write_registers(const AdcRegs &regs) {
  uint8_t cmd[] = {0x43, 0, 0, 0, 0};
  cmd[1] = regs.r0;
  cmd[2] = regs.r1;
  cmd[3] = regs.r2;
  cmd[4] = regs.r3;
  send_command(cmd, sizeof(cmd));
}

// Bfr3 points to three bytes with ADC value.
int32_t decode_int24(const uint8_t *bfr3) {
  const uint32_t sign_extension = (bfr3[0] & 0x80) ? 0xff000000 : 0x00000000;
  const uint32_t value = sign_extension | ((uint32_t)bfr3[0]) << 16 |
                         ((uint32_t)bfr3[1]) << 8 | ((uint32_t)bfr3[2]) << 0;
  return (int32_t)value;
}



// Assuming cs is pulsing.
void start_continuos_DMA() {
  set_dma_request_generator(3);
  continious_DMA = true;
  irq_event_queue.reset();
  irq_error_count = 0;
  irq_half_count = 0;
  irq_full_count = 0;

  
  for (uint32_t i = 0; i < sizeof(tx_buffer); i++) {
    const uint32_t j = i % 3;
    // Every third byte is start comment, for next reading.
    tx_buffer[i] = (j == 0) ? 0x08 : 00;
  }



 

  // Should read 3 bytes but we read for to simulate DMA burst of
  // 4.
  const auto status = HAL_SPI_TransmitReceive_DMA(&hspi1, tx_buffer, rx_buffer,
                                                  sizeof(tx_buffer));
  if (HAL_OK != status) {
    // dma_active = false;
    Error_Handler();
  }

  logger.info("ADC dma started.");
}

static void setup() {

  // Register interrupt handler. These handler are marked in
  // cube ide for registration rather than overriding a weak
  // global handler.
  if (HAL_OK != HAL_SPI_RegisterCallback(&hspi1,
                                         HAL_SPI_TX_RX_HALF_COMPLETE_CB_ID,
                                         spi_TxRxHalfCpltCallback)) {
    Error_Handler();
  }
  if (HAL_OK != HAL_SPI_RegisterCallback(&hspi1, HAL_SPI_TX_RX_COMPLETE_CB_ID,
                                         spi_TxRxCpltCallback)) {
    Error_Handler();
  }
  if (HAL_OK != HAL_SPI_RegisterCallback(&hspi1, HAL_SPI_ERROR_CB_ID,
                                         spi_ErrorCallback)) {
    Error_Handler();
  }

  // for (;;) {
  io::TEST1.high();

  cmd_reset();

  // ADC load cell inputs: p=ain2, n=ain3. Ratiometric measurement
  // with AVDD as reference.
  static const AdcRegs wr_regs = {0x5c, 0xc0, 0xc0, 0x00};
  cmd_write_registers(wr_regs);

  // Sanity check that we wrote the registers correctly.
  AdcRegs rd_regs;
  memset(&rd_regs, 0, sizeof(rd_regs));
  cmd_read_registers(&rd_regs);
  logger.info("Regs: %02hx, %02hx, %02hx, %02hx", rd_regs.r0, rd_regs.r1,
              rd_regs.r2, rd_regs.r3);

  // Start the first conversion.
  cmd_start();



  io::TEST1.low();

  time_util::delay_millis(500);
  logger.info("Setup loop");
  // }

  // Pulsate the CS signal to the ADC. The DMA transactions are synced
  // to this time base.
  // cs_pulsing();
  start_continuos_DMA();
}

void dump_state() {
  uint32_t _irq_half_count;
  uint32_t _irq_full_count;
  uint32_t _irq_error_count;
  uint32_t _event_half_count;
  uint32_t _event_full_count;

  __disable_irq();
  {
    _irq_half_count = irq_half_count;
    _irq_full_count = irq_full_count;
    _irq_error_count = irq_error_count;
    _event_half_count = event_half_count;
    _event_full_count = event_full_count;
  }
  __enable_irq();

  logger.info("DMA counters: half: %lu (%lu), full: %lu (%lu), err: %lu",
              _irq_half_count, _event_half_count, _irq_full_count,
              _event_full_count, _irq_error_count);
 
}

// Elappsed dump_timer;

void process_dma_rx_buffer(int id, uint32_t isr_millis, uint8_t *bfr) {
  const bool reports_enabled = controller::is_adc_report_enabled();
  if (reports_enabled) {
    packet_data.clear();
    packet_data.write_uint8(1);  // version
    packet_data.write_uint32(
        isr_millis);  // Acq end time millis. Assuming systicks = millis.
    packet_data.write_uint16(kPointsPerGroup);  // Expected num of points.
    for (uint32_t i = 0; i < kBytesPerGroup; i += 3) {
      packet_data.write_bytes(&bfr[i], 3);
    }
    if (packet_data.had_write_errors()) {
      Error_Handler();
    }
    host_link::client.sendMessage(HostPorts::ADC_REPORT_MESSAGE, packet_data);
  }

  logger.info("ADC %d: %lx, %lx, %lx, %lx, %lx, %lx, %lx, %lx, %lx, %lx", id,
              decode_int24(&bfr[0]), decode_int24(&bfr[3]),
              decode_int24(&bfr[6]), decode_int24(&bfr[9]),
              decode_int24(&bfr[12]), decode_int24(&bfr[15]),
              decode_int24(&bfr[18]), decode_int24(&bfr[21]),
              decode_int24(&bfr[24]), decode_int24(&bfr[27]));

  logger.info("Processed in %lu ms, reports_enabled: %hd",
              time_util::millis() - isr_millis, reports_enabled);
}

void adc_task_body(void *argument) {
  setup();
  for (;;) {
    IrqEvent event;
    if (!irq_event_queue.consume_from_task(&event, 300)) {
      logger.error("Timeout fetching ADC event.");
      time_util::delay_millis(200);
      continue;
    }
    // logger.info("Event %d", event);
    if (event.id == EVENT_HALF_COMPLETE) {
      event_half_count++;
      process_dma_rx_buffer(0, event.isr_millis, &rx_buffer[0]);
    } else if (event.id == EVENT_FULL_COMPLETE) {
      event_full_count++;
      process_dma_rx_buffer(1, event.isr_millis, &rx_buffer[kBytesPerGroup]);
    } else {
      Error_Handler();
    }
  }
}

}  // namespace adc