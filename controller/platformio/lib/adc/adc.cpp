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

using host_link::HostPorts;

namespace adc {

// Our DMA Terminology
// * Buffer - the entire dma tx or rx buffer (must be same size).
// * Half (buffer) - half of a DMA tx or rx buffer. We use double buffering,
//   transfering one half while processing the results in the other
//   half.
// * Point - a SPI transaction that reads the previous value and starts
//   the next conversion.
// * Cycle - the sequence of points in this order:
//   [LC1, T1, LC1, T2, LC1, T3]..., where LC = a load cell point, and Ti is
//   a point of the respective thermistor channel. This pattern is optimized
//   for high load cell rate and low thermistor rate.

constexpr uint32_t kDmaBytesPerPoint = 13;
constexpr uint32_t kDmaNumThermistorChans = 3;
constexpr uint32_t kDmaNumPointsPerCycle = 2 * kDmaNumThermistorChans;
constexpr uint32_t kDmaBytesPerCycle =
    kDmaNumPointsPerCycle * kDmaBytesPerPoint;
constexpr uint32_t kDmaCyclesPerHalf = 40;
constexpr uint32_t kDmaPointsPerHalf =
    kDmaNumPointsPerCycle * kDmaCyclesPerHalf;
constexpr uint32_t kDmaBytesPerHalf = kDmaPointsPerHalf * kDmaBytesPerPoint;

// The offset of the 3 bytes conversion data within the
// kBytesPerPoint bytes of a single point SPI transfer.
constexpr uint32_t kRxDataOffsetInPoint = 2;

// This is the frequency of TIM2 which generates the points time
// base.
constexpr uint16_t kDmaPointsPerSec = 1000;

// Each of tx/rx buffer contains two halves that are used
// as dual buffers with the DMA circular mode.
static uint8_t tx_buffer[2 * kDmaBytesPerHalf] = {};
static uint8_t rx_buffer[2 * kDmaBytesPerHalf] = {};

static SerialPacketsData packet_data;

// For encoding the data as packets for logging to SD card.
SerialPacketsEncoder packet_encoder;
StuffedPacketBuffer stuffed_packet;

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

  // Confirm the assumptions of the code below.
  static_assert(kDmaNumThermistorChans == 3);
  static_assert(kDmaBytesPerPoint == 13);
  static_assert(kRxDataOffsetInPoint == 2);
  static_assert(kDmaCyclesPerHalf * kDmaNumPointsPerCycle * kDmaBytesPerPoint ==
                kDmaBytesPerHalf);
  static_assert(2 * kDmaBytesPerHalf == sizeof(tx_buffer));
  static_assert(sizeof(tx_buffer) == sizeof(rx_buffer));

  uint8_t *p = tx_buffer;
  // for (uint32_t h = 0; h < 2; h++) {
  // Populate the first half of the TX buffer.
  for (uint32_t cycle = 0; cycle < kDmaCyclesPerHalf; cycle++) {
    for (uint32_t pt = 0; pt < kDmaNumPointsPerCycle; pt++) {
      // Since we set the ADC for convertion that we will
      // read on next SPI transaction, we use the next point.
      // Next point index:
      // 0, 2, 4 -> load cell.
      // 1, 3, 5 -> thermistor 1, 2, 3, respectivly.
      const uint32_t next_pt = (pt + 1) % kDmaNumPointsPerCycle;
      const bool next_is_loadcell = ((next_pt & 0x01) == 0);

      // Reference selection.
      // Load cell: (ain0 - ain1)
      // Thermistors: (avdd - avss)
      const uint8_t reg_0x06_val = next_is_loadcell ? 0x0a : 0x05;

      // PGA selection.
      // Load cell: x128 gain.
      // Thermistors: disabled.
      const uint8_t reg_0x10_val = next_is_loadcell ? 0x07 : 0x80;

      // Input selection.
      // Load cell: (ain1 - ain0)
      // Thermistor 1: (ain4 - ain5)
      // Thermistor 2: (ain6 - ain7)
      // Thermistor 3: (ain8 - ain9)
      const uint8_t reg_0x11_val = next_is_loadcell ? 0x34
                                   : (next_pt == 1) ? 0x56
                                   : (next_pt == 3) ? 0x78
                                                    : 0x9a;

      // RDATA: Read data of previous conversion.
      *p++ = 0x12;
      *p++ = 0x00;  // Dummy.
      *p++ = 0x00;  // Read data byte 1 (MSB) (kRxDataOffsetInPoint)
      *p++ = 0x00;  // Read data byte 2
      *p++ = 0x00;  // Read data byte 3 (LSB)

      // Set and start next converstion.
      // WREG: Write to reg 0x06 (reference selection)
      *p++ = (uint8_t)0x40 | 0x06;
      *p++ = reg_0x06_val;
      // WREG:  Write to reg 0x10 (gain selection)
      *p++ = (uint8_t)0x40 | 0x10;
      *p++ = reg_0x10_val;  //
      // WREG: Write to reg 0x11 (input selection)
      *p++ = (uint8_t)0x40 | 0x11;
      *p++ = reg_0x11_val;
      // START: Start next conversion.
      *p++ = 0x08;
      *p++ = 0x00;  // Dummy byte.
    }
    // }
  }

  // We expect to be here exactly past the first half.
  if (p != (&tx_buffer[kDmaBytesPerHalf])) {
    Error_Handler();
  }

  // Copy the first half to second half.
  memcpy(&tx_buffer[kDmaBytesPerHalf], tx_buffer, kDmaBytesPerHalf);

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

  // Start the continusons DMA. It is set to transfer kDmaBytesPerPoint
  // bytes to the ADC SPI, on each high to low transition of TIM12 output
  // which acts as SPI CS.
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

  // Per the ADS1261 sample code, since we don't monitor the READY
  // signal, we need to wait blindly 50ms for the ADC to settle.
  time_util::delay_millis(50);

  cmd_reset();

  logger.info("ADC device id: 0x%02hx", cmd_read_register(0));

  cmd_write_register(0x01, 0x00);  // Clear CRC and RESET flags.
  cmd_write_register(0x02, 0x5c);  // 4800 SPS, FIR enabled.
  cmd_write_register(0x03, 0x11);  // 50us start delay, one shot.

  // These are temporary values. The real values are set later,
  // per each ADC conversion, by the continuos DMA.
  cmd_write_register(0x06, 0x09);  // Ref Ain0 - Ain1
  cmd_write_register(0x10, 0x07);  // PGA = x128
  cmd_write_register(0x11, 0x34);  // Ain1 = positive, Ain0 = Negative.

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

void process_rx_dma_half_buffer(int id, uint32_t isr_millis, uint8_t *bfr) {
  // const bool reports_enabled = controller::is_adc_report_enabled();
  packet_data.clear();
  packet_data.write_uint8(1);  // packet version
  // NOTE: In case of a millis wrap around, it's ok if this wraps back. All
  // timestamps are mod 2^32.
  const uint32_t packet_base_millis =
      isr_millis - ((1000 * (kDmaPointsPerHalf - 1)) / kDmaPointsPerSec);
  packet_data.write_uint32(packet_base_millis);
  // Write the load cell channel data.
  {
    // Chan id. Load cell 1.
    packet_data.write_uint8(0x11);
    // Offset of first value relative to packet start time.
    packet_data.write_uint16(0);
    // Num of load cell points in this packet.
    static_assert(kDmaPointsPerHalf % 2 == 0);
    packet_data.write_uint16(kDmaPointsPerHalf / 2);
    // Millis between values. We don't allow truncation.
    static_assert(kDmaPointsPerHalf / 2 > 1);
    static_assert(kDmaPointsPerSec % 2 == 0);
    static_assert((2 * 1000) % kDmaPointsPerSec == 0);
    packet_data.write_uint16((2 * 1000) / kDmaPointsPerSec);
    // Write the values. They are already in big endian order.
    for (uint32_t i = 0; i < kDmaPointsPerHalf; i += 2) {
      packet_data.write_bytes(
          &bfr[(i * kDmaBytesPerPoint) + kRxDataOffsetInPoint], 3);
    }
  }

  // Output each of the thermistor channels. Each channel has a single point
  // in each cycle of points.
  for (uint32_t i = 0; i < kDmaNumThermistorChans; i++) {
    // Thermistor channel id.
    packet_data.write_uint8(0x21 + i);
    // First item offset in ms from packet base time. Truncation of
    // a fraction of a ms is ok.
    const uint32_t first_point_index = 1 + 2 * i;
    packet_data.write_uint16((1000 * first_point_index) / kDmaPointsPerSec);
    // Number of points in this channel report. Each thermistor channel
    // has a single point in each cycle.
    packet_data.write_uint16(kDmaCyclesPerHalf);
    // Millis between values. We don't allow truncation.
    static_assert(kDmaCyclesPerHalf > 1);
    static_assert((1000 * kDmaNumPointsPerCycle) % kDmaPointsPerSec == 0);
    packet_data.write_uint16((1000 * kDmaNumPointsPerCycle) / kDmaPointsPerSec);
    // Write the values. They are already in big endian order.
    const uint32_t data_offset_in_cycle =
        (first_point_index * kDmaBytesPerPoint) + kRxDataOffsetInPoint;
    for (uint32_t cycle = 0; cycle < kDmaCyclesPerHalf; cycle++) {
      const uint32_t data_offset_in_bfr =
          (cycle * kDmaBytesPerCycle) + data_offset_in_cycle;
      packet_data.write_bytes(&bfr[data_offset_in_bfr], 3);
    }
  }

  // Verify writing was OK.
  if (packet_data.had_write_errors()) {
    Error_Handler();
  }

  // Send data to host.
  host_link::client.sendMessage(HostPorts::ADC_REPORT_MESSAGE, packet_data);

  // Store data on SD card.
  // io::TEST1.high();
  packet_encoder.encode_log_packet(packet_data, &stuffed_packet);
  sd::append_to_log_file(stuffed_packet);

  // Debugging info.
  logger.info("ADC %d: %lx, %lx, %lx, %lx, %lx", id,
              decode_int24(&bfr[kRxDataOffsetInPoint]),
              decode_int24(&bfr[kRxDataOffsetInPoint + 2 * kDmaBytesPerPoint]),
              decode_int24(&bfr[kRxDataOffsetInPoint + 4 * kDmaBytesPerPoint]),
              decode_int24(&bfr[kRxDataOffsetInPoint + 6 * kDmaBytesPerPoint]),
              decode_int24(&bfr[kRxDataOffsetInPoint + 8 * kDmaBytesPerPoint]));

  logger.info("ADC processed in %lu ms", time_util::millis() - isr_millis);
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