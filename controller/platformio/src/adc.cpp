#include "adc.h"

#include "dma.h"
#include "main.h"
#include "spi.h"

namespace adc {

static uint8_t tx_buffer[10];
static uint8_t rx_buffer[10];

static void trap() {
  // Breakpoint here.
  asm("nop");
}
void spi_TxRxCpltCallback(SPI_HandleTypeDef *hspi) { trap(); }

void spi_TxRxHalfCpltCallback(SPI_HandleTypeDef *hspi) { trap(); }

void spi_ErrorCallback(SPI_HandleTypeDef *hspi) { trap(); }

//----------
void test() {
  if (HAL_OK != HAL_SPI_RegisterCallback(&hspi1, HAL_SPI_TX_RX_COMPLETE_CB_ID,
                                         spi_TxRxCpltCallback)) {
    Error_Handler();
  }
  if (HAL_OK != HAL_SPI_RegisterCallback(&hspi1,
                                         HAL_SPI_TX_RX_HALF_COMPLETE_CB_ID,
                                         spi_TxRxHalfCpltCallback)) {
    Error_Handler();
  }
  if (HAL_OK != HAL_SPI_RegisterCallback(&hspi1, HAL_SPI_ERROR_CB_ID,
                                         spi_ErrorCallback)) {
    Error_Handler();
  }

  if (HAL_OK != HAL_SPI_TransmitReceive_DMA(&hspi1, tx_buffer, rx_buffer,
                                            sizeof(tx_buffer))) {
    Error_Handler();
  }
  
}

}  // namespace adc