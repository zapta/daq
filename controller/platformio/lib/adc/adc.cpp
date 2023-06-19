#include "adc.h"

#include <FreeRtos.h>
#include <portmacro.h>
#include <queue.h>

#include <cstring>

#include "dma.h"
#include "host_link.h"
#include "io.h"
#include "logger.h"
#include "main.h"
#include "serial_packets_client.h"
#include "spi.h"
#include "static_queue.h"
#include "tim.h"
#include "time_util.h"

extern DMA_HandleTypeDef hdma_spi1_tx;

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

// static bool dma_active = false;
static uint32_t irq_half_count = 0;
static uint32_t irq_full_count = 0;
static uint32_t irq_error_count = 0;

static uint32_t event_half_count = 0;
static uint32_t event_full_count = 0;

// In continuous reading we use TIM12 PWM output to trigger the DMA transactions
// and as ADC CS signal. During initialization, we use it as a manual CS
// by setting the PWM duty cycle to either 0% or 100%.
static void cs_high() {
  const uint32_t period = 1 + __HAL_TIM_GET_AUTORELOAD(&htim12);
  __HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_1, period);
  // Let the PWM reload next cycle.
  time_util::delay_millis(5);
}

static void cs_low() {
  __HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_1, 0);
  // Let the PWM reload next cycle.
  time_util::delay_millis(5);
}

// The TIM12 PWM output is pulsating at sampling rate to synchroize
// the DMA transfers.
static void cs_pulsing() {
  __HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_1, 100);
  // Let the PWM reload next cycle.
  time_util::delay_millis(5);
}

static void trap() {
  // Breakpoint here.
  asm("nop");
}

// Called when the first half of rx_buffer is ready for processing.
void spi_TxRxHalfCpltCallback(SPI_HandleTypeDef *hspi) {
  trap();
  irq_half_count++;
  BaseType_t task_woken;
  IrqEvent event = {.id = EVENT_HALF_COMPLETE,
                    .isr_millis = time_util::millis_from_isr()};
  irq_event_queue.add_from_isr(event, &task_woken);
  portYIELD_FROM_ISR(task_woken)
}

// Called when the second half of rx_buffer is ready for processing.
void spi_TxRxCpltCallback(SPI_HandleTypeDef *hspi) {
  trap();
  irq_full_count++;
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

// static void wait_for_dma_completion() {
//   for (;;) {
//     if (!dma_active) {
//       // io::ADC_CS.high();
//       return;
//     }
//     time_util::delay_millis(1);
//   }
// }

// Blocks until completion. Recieved bytes are returned in rx_buffer.
static void send_command(const uint8_t *cmd, uint16_t num_bytes) {
  if (num_bytes > sizeof(rx_buffer)) {
    Error_Handler();
  }
  memset(rx_buffer, 0, num_bytes);
  cs_low();
  HAL_StatusTypeDef status =
      HAL_SPI_TransmitReceive(&hspi1, cmd, rx_buffer, num_bytes, 500);
  if (HAL_OK != status) {
    // dma_active = false;
    Error_Handler();
  }
  cs_high();
}

void cmd_reset() {
  static const uint8_t cmd[] = {0x06};
  send_command(cmd, sizeof(cmd));
  // Datasheet says to wait ~50us.
  time_util::delay_millis(1);
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

int32_t grams(int32_t adc_reading) {
  const int32_t result = (adc_reading - 25000) * (5000.0 / 600000.0);
  // if (result < -500 || result > 500) {
  //   io::TEST1.high();
  // }
  return result;
}

// Assuming cs is pulsing.
void start_DMA() {
  cs_pulsing();

  // io::TEST1.low();

  // memset(rx_buffer, 0, sizeof(rx_buffer));

  // TODO: Create this one during initialization, or use a const
  // build time array.
  for (uint32_t i = 0; i < sizeof(tx_buffer); i++) {
    const uint32_t j = i % 3;
    // Every fourth byte is start comment, for next reading.
    tx_buffer[i] = (j == 0) ? 0x08 : 00;
  }

  // Assert that the DMAMUX request generator is configured to
  // send 3 bytes per trigger. This value comes from the TX DMA setting
  // of the SPI channel, in Cube IDE and is imported as part of spi.c.
  const uint32_t num_requests =
      1 + ((hdma_spi1_tx.DMAmuxChannel->CCR >> 19) & 0x0000001f);
  if (num_requests != 3) {
    Error_Handler();
  }

  // A workaround to reset the DMAMUX request generator.
  // Based on a call to HAL_DMAEx_ConfigMuxSync in spi.c.
  CLEAR_BIT(hdma_spi1_tx.DMAmuxChannel->CCR, (DMAMUX_CxCR_SE));
  SET_BIT(hdma_spi1_tx.DMAmuxChannel->CCR, (DMAMUX_CxCR_SE));

  // dma_active = true;

  // Should read 3 bytes but we read for to simulate DMA burst of
  // 4.
  const auto status = HAL_SPI_TransmitReceive_DMA(&hspi1, tx_buffer, rx_buffer,
                                                  sizeof(tx_buffer));
  if (HAL_OK != status) {
    // dma_active = false;
    Error_Handler();
  }

  logger.info("ADC dma started.");
  // wait_for_dma_completion();

  // logger.info("ADC: %lx, %lx, %lx, %lx, %lx, %lx, %lx, %lx, %lx, %lx",
  //             decode_int24(&rx_buffer[0]), decode_int24(&rx_buffer[3]),
  //             decode_int24(&rx_buffer[6]), decode_int24(&rx_buffer[9]),
  //             decode_int24(&rx_buffer[12]), decode_int24(&rx_buffer[15]),
  //             decode_int24(&rx_buffer[18]), decode_int24(&rx_buffer[21]),
  //             decode_int24(&rx_buffer[24]), decode_int24(&rx_buffer[27]));

  // logger.info(
  //     "Grams: %ld, %ld, %ld, %ld, %ld, %ld, %ld, %ld, %ld, %ld",
  //     grams(decode_int24(&rx_buffer[0])), grams(decode_int24(&rx_buffer[3])),
  //     grams(decode_int24(&rx_buffer[6])), grams(decode_int24(&rx_buffer[9])),
  //     grams(decode_int24(&rx_buffer[12])),
  //     grams(decode_int24(&rx_buffer[15])),
  //     grams(decode_int24(&rx_buffer[18])),
  //     grams(decode_int24(&rx_buffer[21])),
  //     grams(decode_int24(&rx_buffer[24])),
  //     grams(decode_int24(&rx_buffer[27])));

  // return decode_int24(rx_buffer);
}

static void setup() {
  io::TEST1.high();

  cs_high();

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

  // for (int i = 0; i < 3; i++) {
  cmd_reset();
  // }

  // ADC load cell inputs: p=ain2, n=ain3.
  // static const AdcRegs wr_regs = {0x5c, 0xc0, 0x00, 0x00};
  static const AdcRegs wr_regs = {0x5c, 0xc0, 0x30, 0x00};
  // Thermistor inputs p=ain0, n=gnd, 2x10ua current source via ain0.
  // static const AdcRegs wr_regs = {0x80, 0xc0, 0x11, 0x24};
  cmd_write_registers(wr_regs);

  // Sanity check that we wrote the registers correctly.
  AdcRegs rd_regs;
  memset(&rd_regs, 0, sizeof(rd_regs));
  cmd_read_registers(&rd_regs);
  logger.info("Regs: %02hx, %02hx, %02hx, %02hx", rd_regs.r0, rd_regs.r1,
              rd_regs.r2, rd_regs.r3);

  // Start the first conversion.
  cmd_start();

  //   static uint8_t queue_items_static_mem[5] = {};
  // static StaticQueue_t  queue_static_mem;
  // queue_handle = xQueueCreateStatic(sizeof(queue_items_static_mem), 1,
  //                                   queue_items_static_mem,
  //                                   &queue_static_mem);
  // if (queue_handle == nullptr) {
  //   Error_Handler();
  // }

  io::TEST1.low();

  // Pulsate the CS signal to the ADC. The DMA transactions are synced
  // to this time base.
  // cs_pulsing();
  start_DMA();
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
  // const int32_t value =
  // read_data_DMA();
  // logger.info("ADC: %ld", value);
  // logger.info("Grams: %ld", grams(value));
}

// Elappsed dump_timer;

void process_dma_rx_buffer(int id, uint32_t isr_millis, uint8_t *bfr) {
  packet_data.clear();
  packet_data.write_uint8(1); // version
  packet_data.write_uint32(isr_millis);  // Acq end time millis. Assuming systicks = millis.
  packet_data.write_uint16(kPointsPerGroup); // Expected num of points.
  for (uint32_t i = 0; i < kBytesPerGroup; i += 3) {
    packet_data.write_bytes(&bfr[i], 3);
  }
  if (packet_data.had_write_errors()) {
    Error_Handler();
  }
  host_link::client.sendMessage(10, packet_data);
  logger.info("Processed in %lu ms", time_util::millis() - isr_millis);

  logger.info("ADC %d: %lx, %lx, %lx, %lx, %lx, %lx, %lx, %lx, %lx, %lx", id,
              decode_int24(&bfr[0]), decode_int24(&bfr[3]),
              decode_int24(&bfr[6]), decode_int24(&bfr[9]),
              decode_int24(&bfr[12]), decode_int24(&bfr[15]),
              decode_int24(&bfr[18]), decode_int24(&bfr[21]),
              decode_int24(&bfr[24]), decode_int24(&bfr[27]));

  logger.info("Grams %d: %ld, %ld, %ld, %ld, %ld, %ld, %ld, %ld, %ld, %ld", id,
              grams(decode_int24(&bfr[0])), grams(decode_int24(&bfr[3])),
              grams(decode_int24(&bfr[6])), grams(decode_int24(&bfr[9])),
              grams(decode_int24(&bfr[12])), grams(decode_int24(&bfr[15])),
              grams(decode_int24(&bfr[18])), grams(decode_int24(&bfr[21])),
              grams(decode_int24(&bfr[24])), grams(decode_int24(&bfr[27])));
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