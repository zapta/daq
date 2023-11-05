#include "adc.h"

#include <FreeRtos.h>
#include <portmacro.h>
#include <queue.h>

#include <cstring>

#include "common.h"
// #include "controller.h"
#include "data_queue.h"
#include "data_recorder.h"
#include "dma.h"
#include "host_link.h"
#include "serial_packets_client.h"
#include "session.h"
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
// * Point - A single ADC conversion.
// * Slot - A consecutive group of points where we read the load cell
//   channel N times and the next temperature channel one time. We read the
//   load cell N times and use only the last value as a workaround for noise
//   that is injected by switching the ADC between input channels.
// * Cycle - A group of slots in which we read each of the temperaute channels
//   once.
//
// Examples of cycle points when reading the load cell three times. The
// '*' indicates the values we actually use.
// (LC, LC, *Lc, *T1), (LC, LC, *LC, *T2), (LC, LC, *LC, *T3)

constexpr uint32_t kDmaBytesPerPoint = 16;
constexpr uint32_t kDmaNumTemperatureChans = 3;
// We read the load cell N times and use only the last value.
constexpr uint32_t kDmaConsecutiveLcPoints = 3;

constexpr uint32_t kDmaPointsPerSlot = kDmaConsecutiveLcPoints + 1;
constexpr uint32_t kDmaSlotsPerCycle = kDmaNumTemperatureChans;
constexpr uint32_t kDmaPointsPerCycle = kDmaSlotsPerCycle * kDmaPointsPerSlot;
constexpr uint32_t kDmaBytesPerCycle = kDmaPointsPerCycle * kDmaBytesPerPoint;
constexpr uint32_t kDmaCyclesPerHalf = 40;
constexpr uint32_t kDmaSlotsPerHalf = kDmaSlotsPerCycle * kDmaCyclesPerHalf;
constexpr uint32_t kDmaPointsPerHalf = kDmaPointsPerCycle * kDmaCyclesPerHalf;
constexpr uint32_t kDmaBytesPerHalf = kDmaPointsPerHalf * kDmaBytesPerPoint;

// The offset of the 3 bytes conversion data within the
// kBytesPerPoint bytes of a single point SPI transfer.
constexpr uint32_t kDmaRxDataOffsetInPoint = 2;

// The byte offset of the register reading value in each point.
constexpr uint32_t kDmaRegValOffsetInPoint = 7;

// This is the frequency of TIM2 which generates the points time
// base. The effective sampling rate of the load cell is this
// value divided by 3, and for each temp channel, this value
// divided by 9.
constexpr uint16_t kDmaPointsPerSec = 2000;

// Each of tx/rx buffer contains two halves that are used
// as dual buffers with the DMA circular mode.
static uint8_t tx_buffer[2 * kDmaBytesPerHalf] = {};
static uint8_t rx_buffer[2 * kDmaBytesPerHalf] = {};

// static SerialPacketsData packet_data;

// Represent the type of an event that is passed from the ISR handlers to the
// worker thread.
enum IrqEventId {
  EVENT_HALF_COMPLETE = 1,
  EVENT_FULL_COMPLETE = 2,
};

// The event itself.
struct IrqEvent {
  IrqEventId id;
  uint32_t isr_millis;
};

// The completion ISRs pass event to the worker thread using
// this queue.
static StaticQueue<IrqEvent, 5> irq_event_queue;

// The state of the DMA operation.
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

// A static register is an ADS1261 register that is initialized
// once and doesn't change value through the execution of the
// continious ADC sampling.
enum RegisterType { STAT, INFO, DYNM };

struct RegisterInfo {
  const uint8_t idx;
  const RegisterType type;
  const uint8_t val;
};

// The table of the static registers and their values.
static const RegisterInfo regs_info[] = {
    {.idx = 0x00, .type = INFO},
    {.idx = 0x01, .type = INFO},
    {.idx = 0x02, .type = STAT, .val = 0x6C},  // 14400 SPS, (Sinc5)
    {.idx = 0x03, .type = STAT, .val = 0x11},  // One shot, 50us delay
    {.idx = 0x04, .type = STAT, .val = 0x00},  // GPIO 0-3 disconnected
    {.idx = 0x05, .type = STAT, .val = 0x00},  // Disable CRC and status.
    {.idx = 0x06, .type = DYNM},
    {.idx = 0x07, .type = STAT, .val = 0x00},  // No offset calibration
    {.idx = 0x08, .type = STAT, .val = 0x00},  // No offset calibration
    {.idx = 0x09, .type = STAT, .val = 0x00},  // No offset calibration
    {.idx = 0x0a, .type = STAT, .val = 0x00},  // No scale calibration
    {.idx = 0x0b, .type = STAT, .val = 0x00},  // No scale calibration
    {.idx = 0x0c, .type = STAT, .val = 0x40},  // No sclae calibration
    {.idx = 0x0d, .type = STAT, .val = 0xff},  // No current injection
    {.idx = 0x0e, .type = STAT, .val = 0x00},  // No current injection
    {.idx = 0x0f, .type = INFO},
    {.idx = 0x10, .type = DYNM},
    {.idx = 0x11, .type = DYNM},
    {.idx = 0x12, .type = STAT, .val = 0x00},  // No burnout detection
};

static constexpr size_t kNumRegsInfo = sizeof(regs_info) / sizeof(regs_info[0]);

// For diagnostics. Used to verify that the static registers
// in the ADC where not mutated, e.g. due to noise on the bus.
static uint8_t regs_values[kNumRegsInfo] = {};

// Called when the first half of rx_buffer is ready for processing.
void spi_TxRxHalfCpltCallbackIsr(SPI_HandleTypeDef *hspi) {
  // error_handler::Panic(322);

  irq_half_count++;

  // In one time transfer we ignore the half complete since
  // we want to transfer the entire buffer before we stop
  // the DMA.
  if (state != DMA_STATE_ONE_SHOT) {
    BaseType_t task_woken = pdFALSE;
    IrqEvent event = {.id = EVENT_HALF_COMPLETE,
                      .isr_millis = time_util::millis_from_isr()};
    if (!irq_event_queue.add_from_isr(event, &task_woken)) {
      // Comment this out for debugging with breakpoints
      error_handler::Panic(52);
    }
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

  BaseType_t task_woken = pdFALSE;
  IrqEvent event = {.id = EVENT_FULL_COMPLETE,
                    .isr_millis = time_util::millis_from_isr()};
  if (!irq_event_queue.add_from_isr(event, &task_woken)) {
    // Comment this out for debugging with breakpoints
    error_handler::Panic(53);
  }
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
    error_handler::Panic(31);
  }

  // Set tx DMA request generator.

  // A workaround to reset the request generator.
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
    error_handler::Panic(32);
  }

  // time_util::delay_millis(50);

  logger.info("Sending SPI one shot (%hu bytes)", num_bytes);
  if (num_bytes > sizeof(rx_buffer)) {
    error_handler::Panic(33);
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
    error_handler::Panic(34);
  }

  IrqEvent event;
  if (!irq_event_queue.consume_from_task(&event, 300)) {
    error_handler::Panic(35);
  }

  // We expect the IRQ handler that aborted the DMA to also
  // set the state to IDLE.
  if (state != DMA_STATE_IDLE) {
    error_handler::Panic(36);
  }

  // logger.info("IRQ event: %d", event.id);
  if (event.id != IrqEventId::EVENT_FULL_COMPLETE) {
    error_handler::Panic(37);
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
    error_handler::Panic(38);
  }
  const uint8_t cmd_code = (uint8_t)0x20 | reg_index;
  const uint8_t cmd[] = {cmd_code, 0x0, 0x0};
  spi_send_one_shot(cmd, sizeof(cmd));
  return rx_buffer[2];
}

void cmd_write_register(uint8_t reg_index, uint8_t val) {
  if (reg_index > 18) {
    error_handler::Panic(39);
  }
  const uint8_t cmd_code = (uint8_t)0x40 | reg_index;
  const uint8_t cmd[] = {cmd_code, val};
  spi_send_one_shot(cmd, sizeof(cmd));
}

// oid cmd_start_conversion() {
//   static const uint8_t cmd[] = {0x08, 0x00};
//   spi_send_one_shot(cmd, sizeof(cmd));
// }v

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
    error_handler::Panic(41);
  }

  // Confirm the assumptions used the code below.
  static_assert(kDmaNumTemperatureChans == 3);
  static_assert(kDmaBytesPerPoint == 16);
  static_assert(kDmaRxDataOffsetInPoint == 2);
  static_assert(kDmaRegValOffsetInPoint == 7);
  static_assert(kDmaCyclesPerHalf * kDmaPointsPerCycle * kDmaBytesPerPoint ==
                kDmaBytesPerHalf);
  static_assert(2 * kDmaBytesPerHalf == sizeof(tx_buffer));
  static_assert(sizeof(tx_buffer) == sizeof(rx_buffer));

  uint8_t *p = tx_buffer;
  // Populate the first half of the TX buffer.
  for (uint32_t cycle = 0; cycle < kDmaCyclesPerHalf; cycle++) {
    for (uint32_t slot = 0; slot < kDmaSlotsPerCycle; slot++) {
      for (uint32_t pt = 0; pt < kDmaPointsPerSlot; pt++) {
        // On each point, we also read the next register. For diagnostic.
        const uint32_t pt_global_index =
            (cycle * kDmaPointsPerCycle) + (slot * kDmaPointsPerSlot) + pt;
        const uint8_t reg_index = pt_global_index % kNumRegsInfo;

        // Determine if next point is a load cell or temp.
        const bool next_point_is_loadcell = pt != (kDmaPointsPerSlot - 2);

        // Reference selection.
        // Load cell: (ain0 - ain1)
        // Temperature: (avdd - avss)
        const uint8_t reg_0x06_val = next_point_is_loadcell ? 0x0a : 0x05;

        // PGA selection.
        // Load cell: x128 gain.
        // Temperature: disabled.
        const uint8_t reg_0x10_val = next_point_is_loadcell ? 0x07 : 0x00;

        // Input selection.
        // Load cell: (ain1 - ain0)
        // Temperature 1: (ain4 - ain5)
        // Temperature 2: (ain6 - ain7)
        // Temperature 3: (ain8 - ain9)

        const uint8_t reg_0x11_val = next_point_is_loadcell ? 0x34
                                     : (slot == 0)          ? 0x56
                                     : (slot == 1)          ? 0x78
                                                            : 0x9a;

        // RDATA: Read data of previous conversion.
        *p++ = 0x12;
        *p++ = 0x00;  // Dummy.
        *p++ = 0x00;  // Read data byte 1 (MSB) (kRxDataOffsetInPoint)
        *p++ = 0x00;  // Read data byte 2
        *p++ = 0x00;  // Read data byte 3 (LSB)

        // Read the next static register, for diagnostic.
        *p++ = (uint8_t)0x20 | reg_index;
        *p++ = 0x00;  // Dummy byte
        *p++ = 0x00;  // Read value

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
    }
  }

  // We expect to be here exactly past the first half.
  if (p != (&tx_buffer[kDmaBytesPerHalf])) {
    error_handler::Panic(42);
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
  static_assert(sizeof(rx_buffer) == sizeof(tx_buffer));
  const auto status = HAL_SPI_TransmitReceive_DMA(&hspi1, tx_buffer, rx_buffer,
                                                  sizeof(tx_buffer));
  if (HAL_OK != status) {
    // dma_active = false;
    error_handler::Panic(43);
  }

  logger.info("ADC: continuos DMA started.");
}

static void setup() {
  if (!state == DMA_STATE_IDLE) {
    error_handler::Panic(44);
  }

  // Register interrupt handler. These handler are marked in
  // cube ide for registration rather than overriding a weak
  // global handler.
  if (HAL_OK != HAL_SPI_RegisterCallback(&hspi1,
                                         HAL_SPI_TX_RX_HALF_COMPLETE_CB_ID,
                                         spi_TxRxHalfCpltCallbackIsr)) {
    error_handler::Panic(45);
  }
  if (HAL_OK != HAL_SPI_RegisterCallback(&hspi1, HAL_SPI_TX_RX_COMPLETE_CB_ID,
                                         spi_TxRxCpltCallbackIsr)) {
    error_handler::Panic(46);
  }
  if (HAL_OK != HAL_SPI_RegisterCallback(&hspi1, HAL_SPI_ERROR_CB_ID,
                                         spi_ErrorCallbackIsr)) {
    error_handler::Panic(47);
  }

  // Per the ADS1261 sample code, since we don't monitor the READY
  // signal, we need to wait blindly 50ms for the ADC to settle.
  time_util::delay_millis(50);

  cmd_reset();

  logger.info("ADC device id: 0x%02hx", cmd_read_register(0));

  // Reset ADC status.
  cmd_write_register(0x01, 0x00);

  // Init static registers
  for (uint8_t i = 0; i < kNumRegsInfo; i++) {
    const RegisterInfo &reg_info = regs_info[i];
    // Verify index consistency.
    if (i != reg_info.idx) {
      error_handler::Panic(48);
    }
    if (reg_info.type == STAT) {
      cmd_write_register(reg_info.idx, reg_info.val);
    }
  }

  // Initialize dynamic registers. They also also modified
  // as part of the continious ADC/DMA sampling.
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
  // Allocate a data buffer. Non blocking. Guaranteed to be non null.
  data_queue::DataBuffer* data_buffer = data_queue::grab_buffer();
  SerialPacketsData* packet_data = &data_buffer->packet_data();

  // const bool reports_enabled = controller::is_adc_report_enabled();
  packet_data->clear();
  packet_data->write_uint8(1);               // packet version
  packet_data->write_uint32(session::id());  // Device session id.
  // NOTE: In case of a millis wrap around, it's ok if this wraps back. All
  // timestamps are mod 2^32.
  const uint32_t packet_base_millis =
      isr_millis - ((1000 * (kDmaPointsPerHalf - 1)) / kDmaPointsPerSec);
  packet_data->write_uint32(packet_base_millis);
  // Write the load cell channel data.
  // logger.info("dt = %lu", time_util::millis() - packet_base_millis);
  {
    // Chan id. Load cell 1.
    packet_data->write_uint8(0x11);
    // Offset of first value relative to packet start time.
    packet_data->write_uint16(0);
    // Num of load cell points in this packet. One per slot.
    packet_data->write_uint16(kDmaSlotsPerHalf);
    // Millis between load cell values. We don't allow truncation.
    static_assert(kDmaPointsPerSec % kDmaPointsPerSlot == 0);
    static_assert((kDmaPointsPerSlot * 1000) % kDmaPointsPerSec == 0);
    // Every slot has a single loadcell reading.
    packet_data->write_uint16((kDmaPointsPerSlot * 1000) / kDmaPointsPerSec);
    // Write the values. They are already in big endian order.
    // We collect last load cell point of each slot.
    for (uint32_t i = kDmaConsecutiveLcPoints - 1; i < kDmaPointsPerHalf;
         i += kDmaPointsPerSlot) {
      packet_data->write_bytes(
          &bfr[(i * kDmaBytesPerPoint) + kDmaRxDataOffsetInPoint], 3);
    }
  }

  // Output each of the temperature channels. Each channel has a single slot
  // in each cycle.
  for (uint32_t i = 0; i < kDmaNumTemperatureChans; i++) {
    // Temperature channel id.
    packet_data->write_uint8(0x21 + i);
    // First item offset in ms from packet base time. Truncation of
    // a fraction of a ms is ok.
    const uint32_t first_pt_index =
        kDmaConsecutiveLcPoints + i * kDmaPointsPerSlot;
    packet_data->write_uint16((1000 * first_pt_index) / kDmaPointsPerSec);
    // Number of values in this channel report. Each temperature channel
    // has a single slot in each cycle.
    packet_data->write_uint16(kDmaCyclesPerHalf);
    // Millis between values. We don't allow truncation.
    static_assert(kDmaCyclesPerHalf > 1);
    static_assert((1000 * kDmaPointsPerCycle) % kDmaPointsPerSec == 0);
    packet_data->write_uint16((1000 * kDmaPointsPerCycle) / kDmaPointsPerSec);
    // Write the values. They are already in big endian order.
    uint32_t byte_index =
        (first_pt_index * kDmaBytesPerPoint) + kDmaRxDataOffsetInPoint;
    for (uint32_t cycle = 0; cycle < kDmaCyclesPerHalf; cycle++) {
      packet_data->write_bytes(&bfr[byte_index], 3);
      byte_index += kDmaBytesPerCycle;
    }
  }

  // Verify writing was OK.
  if (packet_data->had_write_errors()) {
    error_handler::Panic(49);
  }

  // For diagnostics. Capture the values of the static registers as
  // we read as part of the continious DMA (one static reg value per
  // point)
  for (uint32_t i = 0; i < kNumRegsInfo; i++) {
    regs_values[i] = bfr[i * kDmaBytesPerPoint + kDmaRegValOffsetInPoint];
  }

  // Debugging info.
  if (true) {
    logger.info(
        "ADC %d: %ld, %ld, %ld", id,
        decode_int24(&bfr[kDmaRxDataOffsetInPoint]),
        decode_int24(&bfr[kDmaRxDataOffsetInPoint + 2 * kDmaBytesPerPoint]),
        decode_int24(&bfr[kDmaRxDataOffsetInPoint + 4 * kDmaBytesPerPoint]));
  }

  // Send to monitor and maybe to SD.
  // Do not use 'buffer' beyond this point.
  data_queue::queue_buffer(data_buffer);
  data_buffer = nullptr;
  packet_data = nullptr;


  if (false) {
    logger.info("ADC processed in %lu ms", time_util::millis() - isr_millis);
  }
}

// TODO: Once we confirm that static regs change value (e.g. bus noise when
// shacking the ADC card), change the static regs reads to writes restore
// the desired values.
void verify_registers_vals() {
  logger.info("ADC id reg: 0x%02hx", regs_values[0x00]);
  logger.info("ADC status reg: 0x%02hx", regs_values[0x01]);
  for (uint32_t i = 0; i < kNumRegsInfo; i++) {
    const RegisterInfo &reg_info = regs_info[i];
    if (reg_info.type == STAT && regs_values[i] != reg_info.val) {
      logger.error("ADC Reg %02hx: %02hx -> %02hx", reg_info.idx, reg_info.val,
                   regs_values[i]);
    }
  }
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
      error_handler::Panic(51);
    }
  }
}

}  // namespace adc