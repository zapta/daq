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
#include "sd.h"
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

// Our DMA Terminology
// * Buffer - the entire dma tx or rx buffer (must be same size).
// * Half (buffer) - half of a DMA tx or rx buffer. We use double buffering,
//   transfering one half while processing the results in the other
//   half.
// * Point - the sequence of bytes in a DMA buffer that are used to transact
//   a single channel reading.
// * Cycle - the sequence of bytes in a DMA buffer that are used to transact
//   one point for each ADC channel we use.

constexpr uint32_t kDmaBytesPerPoint = 13;
constexpr uint32_t kNumAdcChans = 1;
constexpr uint32_t kDmaBytesPerCycle = kNumAdcChans * kDmaBytesPerPoint;
constexpr uint32_t kDmaPointsPerChanPerHalf = 100;
constexpr uint32_t kDmaPointsPerHalf = kNumAdcChans * kDmaPointsPerChanPerHalf;
constexpr uint32_t kDmaBytesPerHalf = kDmaPointsPerHalf * kDmaBytesPerPoint;

// The offset of the 3 bytes conversion data within the
// kBytesPerPoint bytes of a single point SPI transfer.
constexpr uint32_t kRxDataOffsetInPoint = 2;

// The number of millis it takes to complete a single reading
// of each channel.
constexpr uint16_t kDmaCycleInternvalMillis = 2;

// Each of tx/rx buffer contains two groups (halves) for cyclic DMA transfer
static uint8_t tx_buffer[2 * kDmaBytesPerHalf] = {};
static uint8_t rx_buffer[2 * kDmaBytesPerHalf] = {};

static SerialPacketsData packet_data;

// For encoding the data as packets for logging to SD card.
SerialPacketsEncoder packet_encoder;
StuffedPacketBuffer stuffed_packet;

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



// TODO: Add a state and support to transition from CONTINUOS to
// IDLE.
enum DmaState {
  // DMA not active.
  DMA_STATE_IDLE,
  // DMA active. Completion IRQ will abort it on first invocation..
  DMA_STATE_ONE_SHOT,
  // DMA is active and half and full completion IRQs are invoked
  // to report completion of respective halves.
  DMA_STATE_CONTINUOS,
};

static volatile DmaState state = DMA_STATE_IDLE;

// General stats.
static volatile uint32_t irq_half_count = 0;
static volatile uint32_t irq_full_count = 0;
static volatile uint32_t irq_error_count = 0;
static volatile uint32_t event_half_count = 0;
static volatile uint32_t event_full_count = 0;

// static bool dma_one_shot = false;

// static void trap() {
//   // Breakpoint here.
//   asm("nop");
// }

// Called when the first half of rx_buffer is ready for processing.
void spi_TxRxHalfCpltCallbackIsr(SPI_HandleTypeDef *hspi) {
  // trap();

  irq_half_count++;

  // In one time transfer we ignore the half complete since
  // we want to transfer the entire buffer before we stop
  // the DMA.
  if (state != DMA_STATE_ONE_SHOT) {
    BaseType_t task_woken;
    IrqEvent event = {.id = EVENT_HALF_COMPLETE,
                      .isr_millis = time_util::millis_from_isr()};
    irq_event_queue.add_from_isr(event, &task_woken);
    portYIELD_FROM_ISR(task_woken)
  }
}

// Called when the second half of rx_buffer is ready for processing.
void spi_TxRxCpltCallbackIsr(SPI_HandleTypeDef *hspi) {
  // trap();

  irq_full_count++;

  // If trasfering a one time transaction, we shut off the
  // DMA transfer once the first transfer is completed.
  if (state == DMA_STATE_ONE_SHOT) {
    HAL_SPI_Abort(&hspi1);
    state = DMA_STATE_IDLE;
  }

  BaseType_t task_woken;
  IrqEvent event = {.id = EVENT_FULL_COMPLETE,
                    .isr_millis = time_util::millis_from_isr()};
  irq_event_queue.add_from_isr(event, &task_woken);
  portYIELD_FROM_ISR(task_woken)
}

void spi_ErrorCallbackIsr(SPI_HandleTypeDef *hspi) {
    // trap();

  irq_error_count++;
}

// Set up the DMA MUX request generator which controls how many
// transfers (bytes in out case) are done on each TIM12 PWM
// sync which we use as a repeating ADC CS.
static void set_dma_request_generator(uint32_t num_transfers_per_sync) {
  if (state != DMA_STATE_IDLE) {
    Error_Handler();
  }

  // Set tx DMA request generator.

  // A woraround to reset the request generator.
  CLEAR_BIT(hdma_spi1_tx.DMAmuxChannel->CCR, (DMAMUX_CxCR_SE));

  const uint32_t mask = DMAMUX_CxCR_NBREQ_Msk;
  MODIFY_REG(hdma_spi1_tx.DMAmuxChannel->CCR, mask,
             (num_transfers_per_sync - 1U) << DMAMUX_CxCR_NBREQ_Pos);

  SET_BIT(hdma_spi1_tx.DMAmuxChannel->CCR, (DMAMUX_CxCR_SE));
}

// Blocks the calling task until completion. Recieved bytes are returned
// in rx_buffer.
static void spi_send_one_shot(const uint8_t *cmd, uint16_t num_bytes) {
  if (state != DMA_STATE_IDLE) {
    Error_Handler();
  }

  // time_util::delay_millis(50);

  logger.info("Sending SPI one shot (%hu bytes)", num_bytes);
  if (num_bytes > sizeof(rx_buffer)) {
    Error_Handler();
  }

  // For determinism.
  memset(rx_buffer, 0, num_bytes);

  // irq_event_queue.reset();

  set_dma_request_generator(num_bytes);
  // continious_DMA = false;
  irq_event_queue.reset();

  state = DMA_STATE_ONE_SHOT;
  const HAL_StatusTypeDef status =
      HAL_SPI_TransmitReceive_DMA(&hspi1, cmd, rx_buffer, num_bytes);
  if (status != HAL_StatusTypeDef::HAL_OK) {
    Error_Handler();
  }

  IrqEvent event;
  if (!irq_event_queue.consume_from_task(&event, 300)) {
    Error_Handler();
  }

  // We expect the IRQ handler that aborted the DMA to also
  // set the state to IDLE.
  if (state != DMA_STATE_IDLE) {
    Error_Handler();
  }

  logger.info("IRQ event: %d", event.id);
  if (event.id != IrqEventId::EVENT_FULL_COMPLETE) {
    Error_Handler();
  }
}

void cmd_reset() {
  static const uint8_t cmd[] = {0x06, 0x00};
  spi_send_one_shot(cmd, sizeof(cmd));

  // time_util::delay_millis(50);
  // Since commands are done at a TIM12 intervals, we don't
  // need to insert a ~50us delay here as called by the datasheet.
}

uint8_t cmd_read_register(uint8_t reg_index) {
  if (reg_index > 18) {
    Error_Handler();
  }
  const uint8_t cmd_code = (uint8_t)0x20 | reg_index;
  const uint8_t cmd[] = {cmd_code, 0x0, 0x0};
  spi_send_one_shot(cmd, sizeof(cmd));
  return rx_buffer[2];
}

void cmd_write_register(uint8_t reg_index, uint8_t val) {
  if (reg_index > 18) {
    Error_Handler();
  }
  const uint8_t cmd_code = (uint8_t)0x40 | reg_index;
  const uint8_t cmd[] = {cmd_code, val};
  spi_send_one_shot(cmd, sizeof(cmd));
}

void cmd_start_conversion() {
  static const uint8_t cmd[] = {0x08, 0x00};
  spi_send_one_shot(cmd, sizeof(cmd));
}

// struct AdcRegs {
//   uint8_t r0;
//   uint8_t r1;
//   uint8_t r2;
//   uint8_t r3;
// };

// void cmd_read_registers(AdcRegs *regs) {
//   static const uint8_t cmd[] = {0x23, 0, 0, 0, 0};
//   // Initializaing with a visible sentinel.
//   // memset(rx_buffer, 0x99, sizeof(rx_buffer));
//   send_command(cmd, sizeof(cmd));
//   regs->r0 = rx_buffer[1];
//   regs->r1 = rx_buffer[2];
//   regs->r2 = rx_buffer[3];
//   regs->r3 = rx_buffer[4];
// }

// static void cmd_write_registers(const AdcRegs &regs) {
//   uint8_t cmd[] = {0x43, 0, 0, 0, 0};
//   cmd[1] = regs.r0;
//   cmd[2] = regs.r1;
//   cmd[3] = regs.r2;
//   cmd[4] = regs.r3;
//   send_command(cmd, sizeof(cmd));
// }

// Bfr3 points to three bytes with ADC value.
int32_t decode_int24(const uint8_t *bfr3) {
  const uint32_t sign_extension = (bfr3[0] & 0x80) ? 0xff000000 : 0x00000000;
  const uint32_t value = sign_extension | ((uint32_t)bfr3[0]) << 16 |
                         ((uint32_t)bfr3[1]) << 8 | ((uint32_t)bfr3[2]) << 0;
  return (int32_t)value;
}

// Assuming cs is pulsing.
void start_continuos_DMA() {
  if (state != DMA_STATE_IDLE) {
    Error_Handler();
  }

  // Send a command to start the first conversion.
  //
  // TODO: This relys on the register settings in setup() which
  // are set seperatly from the setting in this function.
  // This is unclean. Consolidate somehow.
  // cmd_start_conversion();



  // Confirm the assumptions of the code below.
  if (kDmaBytesPerPoint != 13) {
    Error_Handler();
  }
  if (kRxDataOffsetInPoint != 2) {
    Error_Handler();
  }
  if (kDmaPointsPerChanPerHalf * kNumAdcChans * kDmaBytesPerPoint !=
      kDmaBytesPerHalf) {
    Error_Handler();
  }
  if (2 * kDmaBytesPerHalf != sizeof(tx_buffer)) {
    Error_Handler();
  }
  if (sizeof(rx_buffer) != sizeof(tx_buffer)) {
    Error_Handler();
  }

  uint8_t *p = tx_buffer;
  // for (uint32_t h = 0; h < 2; h++) {
  // Populate the first half of the TX buffer.
  for (uint32_t i = 0; i < kDmaPointsPerChanPerHalf; i++) {
    for (uint32_t j = 0; j < kNumAdcChans; j++) {
      // Read data of last conversion.
      *p++ = 0x12;
      *p++ = 0x00;  // Dummy.
      *p++ = 0x00;  // Read data byte 1 (MSB)
      *p++ = 0x00;  // Read data byte 2
      *p++ = 0x00;  // Read data byte 3 (LSB)
      // Write to reg 0x06.
      *p++ = (uint8_t)0x40 | 0x06;
      *p++ = 0x09;  // Ref Ain0 - Ain1
      // Write to reg 0x10
      *p++ = (uint8_t)0x40 | 0x10;
      *p++ = 0x07;  // PGA = x128
      // Write to reg 0x11.
      *p++ = (uint8_t)0x40 | 0x11;
      *p++ = 0x34;  // Ain1 = positive, Ain0 = Negative.
      // Command: start next conversion.
      *p++ = 0x08;
      *p++ = 0x00;  // Dummy byte.
    }
    // }

    // const uint32_t expected_offset = (h == 0) ? kDmaBytesPerHalf :
    // sizeof(tx_buffer); if (p != (tx_buffer + expected_offset)) {
    //   Error_Handler();
    // }
  }

  if (p != (&tx_buffer[kDmaBytesPerHalf])) {
    Error_Handler();
  }

  // Copy first half to second half.
  memcpy(&tx_buffer[kDmaBytesPerHalf], tx_buffer, kDmaBytesPerHalf);

  // tx_buffer[i + 0] = 0x12;  // Command: read data.
  // tx_buffer[i + 1] = 0x00;  // Dummy.
  // tx_buffer[i + 2] = 0x00;  // Read data byte 1 (MSB)
  // tx_buffer[i + 3] = 0x00;  // Read data byte 2
  // tx_buffer[i + 4] = 0x00;  // Read data byte 3 (LSB)
  // tx_buffer[i + 5] = 0x08;  // Command: start conversion.
  // tx_buffer[i + 6] = 0x00;  // Dummy.

  // const uint32_t j = i % 3;
  // // Every third byte is start comment, for next reading.
  // tx_buffer[i] = (j == 0) ? 0x08 : 00;
  // }

  // if (i != sizeof(tx_buffer)) {
  //   Error_Handler();
  // }

  // Issue the last command in the TX buffer to start 
  // the first conversion.
  spi_send_one_shot(&tx_buffer[sizeof(tx_buffer) - kDmaBytesPerPoint], 
  kDmaBytesPerPoint);

  set_dma_request_generator(kDmaBytesPerPoint);

  irq_event_queue.reset();
  irq_error_count = 0;
  irq_half_count = 0;
  irq_full_count = 0;

  state = DMA_STATE_CONTINUOS;

  // Reset RX buffer for determinism.
  memset(rx_buffer, 0, sizeof(rx_buffer));


  const auto status = HAL_SPI_TransmitReceive_DMA(&hspi1, tx_buffer, rx_buffer,
                                                  sizeof(tx_buffer));
  if (HAL_OK != status) {
    // dma_active = false;
    Error_Handler();
  }

  logger.info("ADC continuos dma started.");
}

static void setup() {
  if (!state == DMA_STATE_IDLE) {
    Error_Handler();
  }

  // Register interrupt handler. These handler are marked in
  // cube ide for registration rather than overriding a weak
  // global handler.
  if (HAL_OK != HAL_SPI_RegisterCallback(&hspi1,
                                         HAL_SPI_TX_RX_HALF_COMPLETE_CB_ID,
                                         spi_TxRxHalfCpltCallbackIsr)) {
    Error_Handler();
  }
  if (HAL_OK != HAL_SPI_RegisterCallback(&hspi1, HAL_SPI_TX_RX_COMPLETE_CB_ID,
                                         spi_TxRxCpltCallbackIsr)) {
    Error_Handler();
  }
  if (HAL_OK != HAL_SPI_RegisterCallback(&hspi1, HAL_SPI_ERROR_CB_ID,
                                         spi_ErrorCallbackIsr)) {
    Error_Handler();
  }

  // for (;;) {
  // io::TEST1.high();
  // logger.info("--------");

  // Per the ADS1261 sample code, since we don't monitor the READY
  // signal, we need to wait blindly 50ms for the ADC to settle.
  time_util::delay_millis(50);

  cmd_reset();

  logger.info("ADC device id: 0x%02hx", cmd_read_register(0));

  cmd_write_register(0x01, 0x00);  // Clear CRC and RESET flags.
  cmd_write_register(0x02, 0x5c);  // 4800 SPS, FIR enabled.
  cmd_write_register(0x03, 0x11);  // 50us start delay, one shot.
  // cmd_write_register(0x06, 0x05);  // Ref = VDD - VSS
  cmd_write_register(0x06, 0x09);  // Ref Ain0 - Ain1

  cmd_write_register(0x10, 0x07);  // PGA = x128

  // cmd_write_register(0x11, 0x21);  // Ain1 = positive, Ain0 = Negative.
  cmd_write_register(0x11, 0x34);  // Ain1 = positive, Ain0 = Negative.

  // for (uint8_t r = 0; r < 32; r++) {
  // const uint8_t reg_val = cmd_read_register(0);
  // logger.info("Reg 0:  0x%02hx", reg_val);
  // }
  // io::TEST1.low();
  // time_util::delay_millis(500);
  // }

  // ADC load cell inputs: p=ain2, n=ain3. Ratiometric measurement
  // with AVDD as reference.
  // static const AdcRegs wr_regs = {0x5c, 0xc0, 0xc0, 0x00};
  // cmd_write_registers(wr_regs);

  // Sanity check that we wrote the registers correctly.
  // AdcRegs rd_regs;
  // memset(&rd_regs, 0, sizeof(rd_regs));
  // cmd_read_registers(&rd_regs);
  // logger.info("Regs: %02hx, %02hx, %02hx, %02hx", rd_regs.r0, rd_regs.r1,
  //             rd_regs.r2, rd_regs.r3);

  //  state = STATE_READY;

  // io::TEST1.low();

  // time_util::delay_millis(500);
  // logger.info("Setup loop");
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

void process_rx_dma_half_buffer(int id, uint32_t isr_millis, uint8_t *bfr) {
  // const bool reports_enabled = controller::is_adc_report_enabled();
  packet_data.clear();
  packet_data.write_uint8(1);  // version
  // NOTE: In case of a millis wrap around, it's ok if this wraps back. All
  // timestamps are mod 2^32.
  const uint32_t packet_base_millis =
      isr_millis - (kDmaPointsPerHalf * kDmaCycleInternvalMillis);
  packet_data.write_uint32(packet_base_millis);
  packet_data.write_uint8(4);  // Group id:  Load cell chan 0.
  packet_data.write_uint8(0);  // Millis offset to first value.
  packet_data.write_uint8(
      kDmaCycleInternvalMillis);               // Millis between readings.
  packet_data.write_uint8(kDmaPointsPerHalf);  // Num of points to follow.
  // Write kPointsPerGroup 24 bit values.
  // NOTE: The ADC returns value in big endian format which is
  // the same as our packet_data.write_x() format so we just
  // copy directly the three bytes per value.
  for (uint32_t i = 0; i < kDmaBytesPerHalf; i += kDmaBytesPerCycle) {
    // Pick the 3 bytes conversion data from the SPI transaction buffer.
    packet_data.write_bytes(&bfr[i + kRxDataOffsetInPoint], 3);
  }
  if (packet_data.had_write_errors()) {
    Error_Handler();
  }

  host_link::client.sendMessage(HostPorts::ADC_REPORT_MESSAGE, packet_data);

  io::TEST1.high();
  packet_encoder.encode_log_packet(packet_data, &stuffed_packet);
  if (!sd::append_to_log_file(stuffed_packet)) {
    logger.error("Failed to write ADC log packet to SD.");
  }
  io::TEST1.low();

  logger.info("ADC %d: %lx, %lx, %lx, %lx, %lx", id,
              decode_int24(&bfr[kRxDataOffsetInPoint]),
              decode_int24(&bfr[kRxDataOffsetInPoint + 1 * kDmaBytesPerCycle]),
              decode_int24(&bfr[kRxDataOffsetInPoint + 2 * kDmaBytesPerCycle]),
              decode_int24(&bfr[kRxDataOffsetInPoint + 3 * kDmaBytesPerCycle]),
              decode_int24(&bfr[kRxDataOffsetInPoint + 4 * kDmaBytesPerCycle]));

  logger.info("Processed in %lu ms", time_util::millis() - isr_millis);
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
      // Process first half.
      process_rx_dma_half_buffer(0, event.isr_millis, &rx_buffer[0]);
    } else if (event.id == EVENT_FULL_COMPLETE) {
      event_full_count++;
      // Process second half.
      process_rx_dma_half_buffer(1, event.isr_millis,
                                 &rx_buffer[kDmaBytesPerHalf]);
    } else {
      Error_Handler();
    }
  }
}

}  // namespace adc