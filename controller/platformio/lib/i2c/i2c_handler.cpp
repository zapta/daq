#include "i2c_handler.h"

#include <FreeRtos.h>
#include <i2c.h>

#include "common.h"
#include "error_handler.h"
#include "time_util.h"

// TODO: Change tx/rx to non blocking (irq, dma, etc)

namespace i2c {

static constexpr uint8_t kDeviceAddress = 0x48 << 1;
// static constexpr uint16_t kDefaultTimeout = 50;

static uint8_t data_buffer[20];

void i2c_MasterTxCallbackIsr(I2C_HandleTypeDef* hi2c) {
  // TODO: Implement.
  error_handler::Panic(115);
}

void i2c_MasterRxCallbackIsr(I2C_HandleTypeDef* hi2c) {
  // TODO: Implement.
  error_handler::Panic(116);
}

void i2c_ErrorCallbackIsr(I2C_HandleTypeDef* hi2c) {
  // TODO: Implement.
  error_handler::Panic(117);
}

void i2c_AbortCallbackIsr(I2C_HandleTypeDef* hi2c) {
  // TODO: Implement.
  error_handler::Panic(118);
}

// void dump_state() {}

// static bool write_reg(uint8_t reg, uint16_t value) {
//   // Select register and write in one transaction.
//   data_buffer[0] = reg;
//   data_buffer[1] = (uint8_t)(value >> 8);
//   data_buffer[2] = (uint8_t)value;
//   const HAL_StatusTypeDef status =
//       HAL_I2C_Master_Transmit(&hi2c1, kDeviceAddress, data_buffer, 3, 100);
//   // logger.info("Status: %d", status);
//   return status == HAL_OK;
// }

// static bool start_select_reg_to_read_DMA(uint8_t reg) {
//   // Set address register.
//   data_buffer[0] = reg;
//   const HAL_StatusTypeDef status =
//       HAL_I2C_Master_Transmit_DMA(&hi2c1, kDeviceAddress, data_buffer, 1);
//   return status == HAL_OK;
// }

// TODO: Consider to use   HAL_I2C_Mem_Read_xx()
// static bool select_reg_to_read(uint8_t reg) {
//   // Set address register.
//   data_buffer[0] = reg;
//   const HAL_StatusTypeDef status =
//       HAL_I2C_Master_Transmit(&hi2c1, kDeviceAddress, data_buffer, 1, 100);
//   return status == HAL_OK;
// }

// static bool read_selected_reg(uint16_t* value) {
//   // Read the selected register.
//   data_buffer[0] = 0;
//   data_buffer[2] = 0;
//   const HAL_StatusTypeDef status =
//       HAL_I2C_Master_Receive(&hi2c1, kDeviceAddress, data_buffer, 2, 100);
//   if (status != HAL_OK) {
//     return false;
//   }
//   *value = ((uint16_t)data_buffer[0] << 8) | data_buffer[1];
//   return true;
// }

// TODO: Consider to use   HAL_I2C_Mem_Read_xx()
// static bool read_reg(uint8_t reg, uint16_t* value) {
//   if (!select_reg_to_read(reg)) {
//     return false;
//   }

//   return read_selected_reg(value);
// }

static void setup() {
  // Register interrupt handler. These handler are marked in
  // cube ide for registration rather than overriding a weak
  // global handler.
  if (HAL_OK != HAL_I2C_RegisterCallback(&hi2c1,
                                         HAL_I2C_MASTER_TX_COMPLETE_CB_ID,
                                         i2c_MasterTxCallbackIsr)) {
    error_handler::Panic(111);
  }
  if (HAL_OK != HAL_I2C_RegisterCallback(&hi2c1,
                                         HAL_I2C_MASTER_RX_COMPLETE_CB_ID,
                                         i2c_MasterRxCallbackIsr)) {
    error_handler::Panic(112);
  }
  if (HAL_OK != HAL_I2C_RegisterCallback(&hi2c1, HAL_I2C_ERROR_CB_ID,
                                         i2c_ErrorCallbackIsr)) {
    error_handler::Panic(113);
  }
  if (HAL_OK != HAL_I2C_RegisterCallback(&hi2c1, HAL_I2C_ABORT_CB_ID,
                                         i2c_AbortCallbackIsr)) {
    error_handler::Panic(114);
  }
}

void i2c_task_body(void* argument) {
  setup();

  // Sampling time 1/128 sec. 4.096V full scale. Single mode.
  static constexpr uint16_t kBaseConfig = 0b0000001100100000;

  // static constexpr uint16_t kConfigSetup = kBaseConfig | 0b0100 << 12;
  static constexpr uint16_t kConfigStart0 =
      kBaseConfig | 0b1 << 15 | 0b0100 << 12;
  static constexpr uint16_t kConfigStart1 =
      kBaseConfig | 0b1 << 15 | 0b0101 << 12;

  // bool ok = write_reg(0x01, 0x4383);  // config
  // bool ok = write_reg(0x01, kConfigStart0);

  // if (!ok) {
  //   error_handler::Panic(999);
  // }
  // // bool status = write_reg(0x01, 0xc383);  // start conversion
  // logger.info("i2c write: %s", ok ? "OK" : "ERR");

  int ch = 0;

  // Processing loop.
  for (;;) {
    time_util::delay_millis(100);

    // uint8_t data_buffer[3];

    // uint16_t reg_value = 0;
    // NOTE: the first value is invalid since the ADC was not
    // configured properly yet.
    // bool ok = read_reg(0x0, &reg_value);  // read conversion

    // bool ok = select_reg_to_read(0);

    // Select ADC's register 0 for reading.
    static_assert(sizeof(data_buffer[0]) == 1);
    static_assert(sizeof(data_buffer) / sizeof(data_buffer[0]) >= 1);
    data_buffer[0] = 0;
    HAL_StatusTypeDef status =
        HAL_I2C_Master_Transmit(&hi2c1, kDeviceAddress, data_buffer, 1, 100);

    // Read selected register
    if (status != HAL_OK) {
      logger.error("I2C: error selecting reg 0: %d", status);
      continue;
    }
    // status = read_selected_reg(&reg_value);
    static_assert(sizeof(data_buffer[0]) == 1);
    static_assert(sizeof(data_buffer) / sizeof(data_buffer[0]) >= 2);
    data_buffer[0] = 0;
    data_buffer[1] = 0;
    status =
        HAL_I2C_Master_Receive(&hi2c1, kDeviceAddress, data_buffer, 2, 100);
    if (status != HAL_OK) {
      logger.error("I2C: error reading reg 0: %d", status);
      continue;
    }

    const uint16_t reg_value = ((uint16_t)data_buffer[0] << 8) | data_buffer[1];
    const int16_t adc_value = (int16_t)reg_value;

    // const int16_t adc_value = (int16_t)reg_value;
    logger.info("I2C read [%d]: %03hx, %hd", ch, reg_value, adc_value);

    //  Toggle channel.
    ch = (ch + 1) & 0x01;

    // Temp for test
    // bool status = write_reg(0x01, 0x4383);

    // Configure mux for next channel and start conversion.
    const uint16_t config_value = ch == 0 ? kConfigStart0 : kConfigStart1;
    static_assert(sizeof(data_buffer[0]) == 1);
    static_assert(sizeof(data_buffer) / sizeof(data_buffer[0]) >= 3);
    data_buffer[0] = 0x01;  // config reg address
    data_buffer[1] = (uint8_t)(config_value >> 8);
    data_buffer[2] = (uint8_t)config_value;
    status =
        HAL_I2C_Master_Transmit(&hi2c1, kDeviceAddress, data_buffer, 3, 100);
    // logger.info("Status: %d", status);

    if (status != HAL_OK) {
      logger.error("I2C: error selecting chanel: %d (%d)", ch, status);
      continue;
    }

    logger.error("I2C: cycle OK");

    // return status == HAL_OK;

    // status = write_reg(
    //     0x01, ch == 0 ? kConfigStart0 : kConfigStart1);  // start conversion
    // logger.info("i2c start conv: %s", ok ? "OK" : "ERR");
  }
}

}  // namespace i2c