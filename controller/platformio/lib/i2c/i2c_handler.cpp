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
  // logger.info("Status: %d", status);
  return status == HAL_OK;
}

// TODO: Consider to use   HAL_I2C_Mem_Read_xx()
static bool read_reg(uint8_t reg, uint16_t* value) {
  // Set address register.
  data_buffer[0] = reg;
  HAL_StatusTypeDef status =
      HAL_I2C_Master_Transmit(&hi2c1, kDeviceAddress, data_buffer, 1, 100);
  if (status != HAL_OK) {
    return false;
  }

  // Read the selected register.
  data_buffer[0] = 0;
  data_buffer[2] = 0;
  status = HAL_I2C_Master_Receive(&hi2c1, kDeviceAddress, data_buffer, 2, 100);
  if (status != HAL_OK) {
    return false;
  }
  *value = ((uint16_t)data_buffer[0] << 8) | data_buffer[1];
  return true;
}

void i2c_task_body(void* argument) {
  // TODO: Add setup.

  // Sampling time 1/128 sec. 4.096V full scale. Single mode.
  static constexpr uint16_t kBaseConfig = 0b0000001100100000;

  // static constexpr uint16_t kConfigSetup = kBaseConfig | 0b0100 << 12;
  static constexpr uint16_t kConfigStart0 =
      kBaseConfig | 0b1 << 15 | 0b0100 << 12;
  static constexpr uint16_t kConfigStart1 =
      kBaseConfig | 0b1 << 15 | 0b0101 << 12;

  // bool ok = write_reg(0x01, 0x4383);  // config
  bool ok = write_reg(0x01, kConfigStart0);

  if (!ok) {
    error_handler::Panic(999);
  }
  // bool status = write_reg(0x01, 0xc383);  // start conversion
  logger.info("i2c write: %s", ok ? "OK" : "ERR");

  int ch = 0;

  // Processing loop.
  for (;;) {
      time_util::delay_millis(100);
      // logger.info("i2c_task_body...");

      uint16_t reg_value = 0;
      ok = read_reg(0x0, &reg_value);  // read conversion
      const int16_t adc_value = (int16_t)reg_value;
      logger.info("i2c read [%d]: %s, %03hx, %hd", ch, ok ? "OK" : "ERR", reg_value,
                  adc_value);

      ch = (ch + 1) & 0x01;

      // Temp for test
      // bool status = write_reg(0x01, 0x4383);
      ok = write_reg(0x01, ch == 0 ? kConfigStart0 : kConfigStart1);  // start conversion
      logger.info("i2c start conv: %s", ok ? "OK" : "ERR");
    }
  
}

}  // namespace i2c