#include "adc.h"

#include <cstring>

#include "dma.h"
#include "io.h"
#include "logger.h"
#include "main.h"
#include "spi.h"
#include "time_util.h"

namespace adc {

static uint8_t rx_buffer[10];

static void trap() {
  // Breakpoint here.
  asm("nop");
}

static bool dma_active = false;

void spi_TxRxCpltCallback(SPI_HandleTypeDef *hspi) {
  dma_active = false;
  io::ADC_CS.high();
  trap();
}

void spi_ErrorCallback(SPI_HandleTypeDef *hspi) {
  dma_active = false;
  io::ADC_CS.high();
  trap();
}

static void wait_for_completion() {
  for (;;) {
    if (!dma_active) {
      // io::ADC_CS.high();
      return;
    }
    time_util::delay_millis(1);
  }
}

// Blocks until completion. Recieved bytes are returned in rx_buffer.
static void send_command(const uint8_t *cmd, uint16_t cmd_len) {
  if (dma_active) {
    Error_Handler();
  }
  if (cmd_len > sizeof(rx_buffer)) {
    Error_Handler();
  }
  memset(rx_buffer, 0, cmd_len);
  dma_active = true;
  io::ADC_CS.low();
  if (HAL_OK != HAL_SPI_TransmitReceive_DMA(&hspi1, cmd, rx_buffer, cmd_len)) {
    dma_active = false;
    Error_Handler();
  }
  wait_for_completion();
  io::ADC_CS.high();
}

void cmd_reset() {
  static const uint8_t cmd[] = {0x06};
  send_command(cmd, sizeof(cmd));
  // Datasheet says to wait ~50us.
  time_util::delay_millis(1);
}

struct Regs {
  uint8_t r0;
  uint8_t r1;
  uint8_t r2;
  uint8_t r3;
};

void cmd_read_registers(Regs *regs) {
  // Command code for reading 4 registers starting from register 0.
  const uint8_t cmd_code = 0x20 | (0 << 2) | 0x03;
  static const uint8_t cmd[] = {cmd_code, 0, 0, 0, 0};
  memset(rx_buffer, 0x66, sizeof(rx_buffer));
  send_command(cmd, sizeof(cmd));
  regs->r0 = rx_buffer[1];
  regs->r1 = rx_buffer[2];
  regs->r2 = rx_buffer[3];
  regs->r3 = rx_buffer[4];
}

void cmd_write_registers(const Regs &regs) {
  // Command code for writing 4 registers starting from register 0.
  const uint8_t cmd_code = 0x40 | (0 << 2) | 0x03;
  static uint8_t cmd[] = {cmd_code, 0, 0, 0, 0};
  cmd[1] = regs.r0;
  cmd[2] = regs.r1;
  cmd[3] = regs.r2;
  cmd[4] = regs.r3;
  send_command(cmd, sizeof(cmd));
}

int32_t cmd_read_data() {
  static const uint8_t cmd[] = {0, 0, 0};
  send_command(cmd, sizeof(cmd));
  uint8_t sign_extension = (rx_buffer[0] & 80) ? 0xff : 0x00;
  return ((int32_t)(sign_extension << 24)) | (((int32_t)rx_buffer[0]) << 16) |
         (((int32_t)rx_buffer[1]) << 8) | ((int32_t)rx_buffer[2]);
}

void test_setup() {
  io::ADC_CS.high();

  if (HAL_OK != HAL_SPI_RegisterCallback(&hspi1, HAL_SPI_TX_RX_COMPLETE_CB_ID,
                                         spi_TxRxCpltCallback)) {
    Error_Handler();
  }
  // if (HAL_OK != HAL_SPI_RegisterCallback(&hspi1,
  //                                        HAL_SPI_TX_RX_HALF_COMPLETE_CB_ID,
  //                                        spi_TxRxHalfCpltCallback)) {
  //   Error_Handler();
  // }

  if (HAL_OK != HAL_SPI_RegisterCallback(&hspi1, HAL_SPI_ERROR_CB_ID,
                                         spi_ErrorCallback)) {
    Error_Handler();
  }

  cmd_reset();

  static const Regs regs = {0x0a, 0xc4, 0x00, 0x02};
  cmd_write_registers(regs);

  static const uint8_t cmd[] = {0x08};
  send_command(cmd, sizeof(cmd));
}

void test_loop() {
  const int32_t value = cmd_read_data();
  logger.info("ADC: %ld", value);
}

}  // namespace adc