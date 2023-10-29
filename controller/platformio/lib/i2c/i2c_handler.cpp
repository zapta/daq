#include "i2c_handler.h"

#include <FreeRtos.h>
#include <i2c.h>

#include "common.h"
#include "time_util.h"

// TODO: Change tx/rx to non blocking (irq, dma, etc)

namespace i2c {

static constexpr uint8_t kDeviceAddress = 0x48 << 1;
static constexpr uint16_t kDefaultTimeout = 50;
static uint8_t data_buffer[20];

void dump_state() {}

static bool write_reg(uint8_t reg, uint16_t value) {
  // Select register and write in one transaction.
  data_buffer[0] = reg;
  data_buffer[1] = (uint8_t)(value >> 8);
  data_buffer[2] = (uint8_t)value;
  const HAL_StatusTypeDef status =
      HAL_I2C_Master_Transmit(&hi2c1, kDeviceAddress, data_buffer, 3, 100);
  logger.info("Status: %d", status);
  return status == HAL_OK;
}

// static bool read_reg(uint8_t reg, uint16_t* value) {
//   // Set address register.
//   data_buffer[0] = reg;
//    HAL_StatusTypeDef status =
//       HAL_I2C_Master_Transmit(&hi2c1, kDeviceAddress, data_buffer, 1, 100);
//   if (status != HAL_OK) {
//     return false;
//   }

//   // Read the selected register.
//   data_buffer[0] = 0;
//   data_buffer[2] = 0;
//    status =
//       HAL_I2C_Master_Receive(&hi2c1, kDeviceAddress, data_buffer, 2, 100);
//   if (status != HAL_OK) {
//     return false;
//   }
//   *value = ((uint16_t)data_buffer[0] << 8) | data_buffer[1];
//   return true;
// }

void i2c_task_body(void* argument) {
  // TODO: Add setup.

  // Processing loop.
  for (;;) {
    time_util::delay_millis(500);
    logger.info("i2c_task_body...");
    
    // Temp for test 
    bool status = write_reg(0x01, 0x4373);
    logger.info("i2c write: %d", (int)status);
  }
}

}  // namespace i2c