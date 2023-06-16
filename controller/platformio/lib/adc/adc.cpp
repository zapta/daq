#include "adc.h"

#include <cstring>

#include "dma.h"
#include "io.h"
#include "logger.h"
#include "main.h"
#include "spi.h"
#include "time_util.h"

namespace adc {

// static const uint8_t tx_buffer[30] = {};
// static uint8_t rx_buffer[30];
static uint32_t tx_word_buffer[10] = {};
static uint32_t rx_word_buffer[10] = {};

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

void spi_RxCpltCallback(SPI_HandleTypeDef *hspi) {
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
// static void x_send_only_command(const uint8_t *cmd, uint16_t cmd_len) {
//   // if (dma_active) {
//   //   Error_Handler();
//   // }
//   // if (cmd_len > sizeof(rx_buffer)) {
//   //   Error_Handler();
//   // }
//   // memset(rx_buffer, 0, cmd_len);
//   // dma_active = true;
//   io::ADC_CS.low();
//   HAL_StatusTypeDef status = HAL_SPI_Transmit(&hspi1, cmd,  cmd_len, 500);
//   if (HAL_OK != status) {
//     dma_active = false;
//     Error_Handler();
//   }
//   // wait_for_completion();
//   io::ADC_CS.high();
// }

// Blocks until completion. Recieved bytes are returned in rx_buffer.
static void send_command(const uint32_t *cmd, uint16_t num_words) {
  // if (dma_active) {
  //   Error_Handler();
  // }
  if (num_words > (sizeof(rx_word_buffer) / sizeof(rx_word_buffer[0]))) {
    Error_Handler();
  }
  memset(rx_word_buffer, 0, sizeof(rx_word_buffer));
  // dma_active = true;
  io::ADC_CS.low();
  HAL_StatusTypeDef status = HAL_SPI_TransmitReceive(
      &hspi1, (const uint8_t *)cmd, (uint8_t *)rx_word_buffer, num_words, 500);
  if (HAL_OK != status) {
    // dma_active = false;
    Error_Handler();
  }
  // wait_for_completion();
  io::ADC_CS.high();
}

// Blocks until completion. Recieved bytes are returned in rx_buffer.
// static void send_command(const uint8_t *cmd, uint16_t cmd_len) {
//   if (dma_active) {
//     Error_Handler();
//   }
//   if (cmd_len > sizeof(rx_buffer)) {
//     Error_Handler();
//   }
//   memset(rx_buffer, 0, cmd_len);
//   dma_active = true;
//   io::ADC_CS.low();
//   const auto status =
//       HAL_SPI_TransmitReceive_DMA(&hspi1, cmd, rx_buffer, cmd_len);
//   if (HAL_OK != status) {
//     io::ADC_CS.high();
//     dma_active = false;
//     Error_Handler();
//   }
//   wait_for_completion();
//   io::ADC_CS.high();
// }

// void cmd_reset() {
//   static const uint8_t cmd[] = {0x06};
//   send_command(cmd, sizeof(cmd));
//   // Datasheet says to wait ~50us.
//   time_util::delay_millis(1);
// }

void cmd_reset() {
  // static const uint8_t cmd[] = {0x06};
  static const uint32_t cmd = 0x06000000;
  // send_command((uint8_t*)&cmd, sizeof(cmd));
  send_command(&cmd, 1);
  // Datasheet says to wait ~50us.
  time_util::delay_millis(1);
}

// void cmd_start() {
//   static const uint8_t cmd[] = {0x08};
//   send_command(cmd, sizeof(cmd));
// }

void cmd_start() {
  // static const uint8_t cmd[] = {0x08};
  static const uint32_t cmd = 0x08000000;
  // send_command(cmd, sizeof(cmd));
  send_command(&cmd, 1);
}

struct Regs {
  uint8_t r0;
  uint8_t r1;
  uint8_t r2;
  uint8_t r3;
};

// void cmd_read_registers(Regs *regs) {
//   // Command code for reading 4 registers starting from register 0.
//   const uint8_t cmd_code = 0x20 | (0 << 2) | 0x03;
//   static const uint8_t cmd[] = {cmd_code, 0, 0, 0, 0};
//   memset(rx_buffer, 0x66, sizeof(rx_buffer));
//   send_command(cmd, sizeof(cmd));
//   regs->r0 = rx_buffer[1];
//   regs->r1 = rx_buffer[2];
//   regs->r2 = rx_buffer[3];
//   regs->r3 = rx_buffer[4];
// }

void cmd_read_registers(Regs *regs) {
  // Command code for reading 4 registers starting from register 0.
  // const uint8_t cmd_code = 0x20 | (0 << 2) | 0x03;
  // static const uint8_t cmd[] = {cmd_code, 0, 0, 0, 0};
  // static const uint8_t cmd[] = {cmd_code, 0, 0, 0, 0};
  static const uint32_t cmd[2] = {0x23000000, 0x00000000};
  memset(rx_word_buffer, 0x66, sizeof(rx_word_buffer));
  send_command(cmd, 2);
  regs->r0 = rx_word_buffer[0] >> 16;
  regs->r1 = rx_word_buffer[0] >> 8;
  regs->r2 = rx_word_buffer[0] >> 0;
  regs->r3 = rx_word_buffer[1] >> 24;
}

// void cmd_write_registers(const Regs &regs) {
//   // Command code for writing 4 registers starting from register 0.
//   const uint8_t cmd_code = 0x40 | (0 << 2) | 0x03;
//   static uint8_t cmd[] = {cmd_code, 0, 0, 0, 0};
//   cmd[1] = regs.r0;
//   cmd[2] = regs.r1;
//   cmd[3] = regs.r2;
//   cmd[4] = regs.r3;
//   send_command(cmd, sizeof(cmd));
// }

static void cmd_write_registers(const Regs &regs) {
  // Command code for writing 4 registers starting from register 0.
  // const uint8_t cmd_code = 0x40 | (0 << 2) | 0x03;
  uint32_t cmd[2] = {0x43000000, 0x00000000};

  // static uint8_t cmd[] = {cmd_code, 0, 0, 0, 0};
  cmd[0] |= (uint32_t)regs.r0 << 16;
  cmd[0] |= (uint32_t)regs.r1 << 8;
  cmd[0] |= (uint32_t)regs.r2 << 0;
  cmd[1] |= (uint32_t)regs.r3 << 24;

  // cmd[1] = regs.r0;
  // cmd[2] = regs.r1;
  // cmd[3] = regs.r2;
  // cmd[4] = regs.r3;
  send_command(cmd, 2);
}

int32_t read_data_DMA() {
  // static const uint8_t cmd[] = {0, 0, 0};
  // send_command(cmd, sizeof(cmd));

  memset(rx_word_buffer, 0, sizeof(rx_word_buffer));
  memset(tx_word_buffer, 0, sizeof(tx_word_buffer));

  dma_active = true;
  io::ADC_CS.low();
  // Should read 3 bytes but we read for to simulate DMA burst of
  // 4.
  const auto status = HAL_SPI_TransmitReceive_DMA(
      &hspi1, (uint8_t *)tx_word_buffer, (uint8_t *)rx_word_buffer, 2);
  if (HAL_OK != status) {
    dma_active = false;
    io::ADC_CS.high();

    Error_Handler();
  }

  wait_for_completion();
  io::ADC_CS.high();

  // uint32_t sign_extension =
  //     (rx_word_buffer[0] & 0x80000000) ? 0xff000000 : 0x00000000;
  // uint32_t value = sign_extension | ((rx_word_buffer[0] >> 8) & 0x00ffffff);
  // return (int32_t)value;

  uint32_t sign_extension =
      (rx_word_buffer[1] & 0x80000000) ? 0xff000000 : 0x00000000;
  uint32_t value = sign_extension | ((rx_word_buffer[1] >> 8) & 0x00ffffff);
  return (int32_t)value;


  // return ((int32_t)(sign_extension << 24)) | (((rx_buffer[0]) << 16) |
  //        (((int32_t)rx_buffer[1]) << 8) | ((int32_t)rx_buffer[2]);
}

// int32_t read_data_blocking() {
//   // static const uint8_t cmd[] = {0, 0, 0};
//   // send_command(cmd, sizeof(cmd));

//   memset(rx_buffer, 0, sizeof(rx_buffer));
//   // dma_active = true;
//   io::ADC_CS.low();
//   // Should read 3 bytes but we read for to simulate DMA burst of
//   // 4.
//   auto status = HAL_SPI_Receive(&hspi1, rx_buffer, 9, 500);
//   io::ADC_CS.high();

//   if (HAL_OK != status) {
//     // dma_active = false;
//     Error_Handler();
//   }

//   logger.info("RX: %02hx %02hx %02hx, %02hx ", rx_buffer[0], rx_buffer[1],
//               rx_buffer[2], rx_buffer[3]);
//   // wait_for_completion();

//   uint8_t sign_extension = (rx_buffer[0] & 80) ? 0xff : 0x00;
//   return ((int32_t)(sign_extension << 24)) | (((int32_t)rx_buffer[0]) << 16)
//   |
//          (((int32_t)rx_buffer[1]) << 8) | ((int32_t)rx_buffer[2]);
// }

void test_setup() {
  io::ADC_CS.high();

  // HAL_SPI_Init(&hspi1);

  if (HAL_OK != HAL_SPI_RegisterCallback(&hspi1, HAL_SPI_TX_RX_COMPLETE_CB_ID,
                                         spi_TxRxCpltCallback)) {
    Error_Handler();
  }

  if (HAL_OK != HAL_SPI_RegisterCallback(&hspi1, HAL_SPI_RX_COMPLETE_CB_ID,
                                         spi_RxCpltCallback)) {
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

  // cmd_reset();
  cmd_reset();
  time_util::delay_millis(500);

  // static const Regs wr_regs = {0x0a, 0xc4, 0x00, 0x02};
  // for (;;) {
    static const Regs wr_regs = {0x0c, 0xc4, 0x00, 0x02};
    cmd_write_registers(wr_regs);
  // }

  Regs rd_regs;
  memset(&rd_regs, 0, sizeof(rd_regs));
  // for(;;) {
  cmd_read_registers(&rd_regs);
  logger.info("Regs: %02hx, %02hx, %02hx, %02hx", rd_regs.r0, rd_regs.r1,
              rd_regs.r2, rd_regs.r3);
              time_util::delay_millis(500);
  // }

  // for(;;) {
  cmd_start();
  // }
}

void test_loop() {
  const int32_t value = read_data_DMA();
  // const int32_t value = read_data_blocking();
  logger.info("ADC: %ld", value);
  int v1 = value - 25000;
  int v2 = v1 * (5000.0 / 600000.0);
  logger.info("Grams: %d", v2);
}

}  // namespace adc