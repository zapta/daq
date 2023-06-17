#include "adc.h"

#include <cstring>

#include "dma.h"
#include "io.h"
#include "logger.h"
#include "main.h"
#include "spi.h"
#include "tim.h"
#include "time_util.h"

extern DMA_HandleTypeDef hdma_spi1_tx;

namespace adc {

// In continuous reading we use TIM12 PWM output to trigger the DMA transactions
// and as ADC CS signal. During initialization, when we configure the ADC using
// non DMA transactions, we control the CS by setting the TIM12 PWM ratio
// between 0 (output is 100% low) to 1000 (output is 100% high).
static void cs_high() {
  __HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_1, 1000);
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

// static const uint8_t tx_buffer[30] = {};
// static uint8_t rx_buffer[30];
static uint8_t tx_buffer[300] = {};
static uint8_t rx_buffer[300] = {};

static void trap() {
  // Breakpoint here.
  asm("nop");
}

static bool dma_active = false;

void spi_TxRxCpltCallback(SPI_HandleTypeDef *hspi) {
  dma_active = false;
  trap();
}

void spi_ErrorCallback(SPI_HandleTypeDef *hspi) {
  dma_active = false;
  trap();
}

static void wait_for_dma_completion() {
  for (;;) {
    if (!dma_active) {
      // io::ADC_CS.high();
      return;
    }
    time_util::delay_millis(1);
  }
}

// Blocks until completion. Recieved bytes are returned in rx_buffer.
static void send_command(const uint8_t *cmd, uint16_t num_bytes) {
  if (num_bytes > sizeof(rx_buffer)) {
    Error_Handler();
  }
  memset(rx_buffer, 0, sizeof(rx_buffer));
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
  memset(rx_buffer, 0x99, sizeof(rx_buffer));
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
  if (result < -500 || result > 500) {
    io::TEST1.high();
  }
  return result;
}

// Assuming cs is pulsing.
int32_t read_data_DMA() {
  io::TEST1.low();

  memset(rx_buffer, 0, sizeof(rx_buffer));

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

  dma_active = true;

  // Should read 3 bytes but we read for to simulate DMA burst of
  // 4.
  const auto status =
      HAL_SPI_TransmitReceive_DMA(&hspi1, tx_buffer, rx_buffer, 30);
  if (HAL_OK != status) {
    dma_active = false;
    Error_Handler();
  }

  wait_for_dma_completion();
 

  logger.info("Readings: %lx, %lx, %lx, %lx, %lx, %lx, %lx, %lx, %lx, %lx",
              decode_int24(&rx_buffer[0]), decode_int24(&rx_buffer[3]),
              decode_int24(&rx_buffer[6]), decode_int24(&rx_buffer[9]),
              decode_int24(&rx_buffer[12]), decode_int24(&rx_buffer[15]),
              decode_int24(&rx_buffer[18]), decode_int24(&rx_buffer[21]),
              decode_int24(&rx_buffer[24]), decode_int24(&rx_buffer[27]));

  logger.info(
      "Readings: %ld, %ld, %ld, %ld, %ld, %ld, %ld, %ld, %ld, %ld",
      grams(decode_int24(&rx_buffer[0])), grams(decode_int24(&rx_buffer[3])),
      grams(decode_int24(&rx_buffer[6])), grams(decode_int24(&rx_buffer[9])),
      grams(decode_int24(&rx_buffer[12])), grams(decode_int24(&rx_buffer[15])),
      grams(decode_int24(&rx_buffer[18])), grams(decode_int24(&rx_buffer[21])),
      grams(decode_int24(&rx_buffer[24])), grams(decode_int24(&rx_buffer[27])));

  
  return decode_int24(rx_buffer);

 
}



void test_setup() {
 

  cs_high();


  // Register interrupt handler. These handler are marked in 
  // cube ide for registration rather than overriding a weak
  // global handler.
  if (HAL_OK != HAL_SPI_RegisterCallback(&hspi1, HAL_SPI_TX_RX_COMPLETE_CB_ID,
                                         spi_TxRxCpltCallback)) {
    Error_Handler();
  }
  if (HAL_OK != HAL_SPI_RegisterCallback(&hspi1, HAL_SPI_ERROR_CB_ID,
                                         spi_ErrorCallback)) {
    Error_Handler();
  }


  cmd_reset();
 

  // Configure ADC registers. We use a single short mode and 
  // start a new conversion on each reading.
  static const AdcRegs wr_regs = {0x0c, 0xc0, 0x00, 0x02};
  cmd_write_registers(wr_regs);

  // Sanity check that we wrote the registers correctly.
  AdcRegs rd_regs;
  memset(&rd_regs, 0, sizeof(rd_regs));
  cmd_read_registers(&rd_regs);
  logger.info("Regs: %02hx, %02hx, %02hx, %02hx", rd_regs.r0, rd_regs.r1,
              rd_regs.r2, rd_regs.r3);
 
  // Start the first conversion.
  cmd_start();
  
  // Pulsate the CS signal to the ADC. The DMA transactions are synced
  // to this time base.
  cs_pulsing();
}

void test_loop() {
  const int32_t value = read_data_DMA();
  logger.info("ADC: %ld", value);
  logger.info("Grams: %ld", grams(value));
}

}  // namespace adc