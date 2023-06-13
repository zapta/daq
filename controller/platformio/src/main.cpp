
#include "main.h"

#include <unistd.h>

#include "FreeRTOS.h"
#include "adc.h"
#include "cdc_serial.h"
#include "dma.h"
#include "gpio.h"
#include "host_link.h"
#include "io.h"
#include "logger.h"
#include "serial.h"
#include "spi.h"
#include "static_task.h"
#include "usart.h"
#include "usbd_cdc_if.h"

static SerialPacketsData data;

StaticTask<2000> host_link_rx_task(host_link::rx_task_body, "Host RX", 10);

// Called from from the main FreeRTOS task.
void app_main() {
  serial::serial1.init();

  host_link::setup(serial::serial1);
  if (!host_link_rx_task.start()) {
    Error_Handler();
  }

  adc::test_setup();

  for (int i = 1;; i++) {
    adc::test_loop();
    io::LED.toggle();
    data.clear();
    data.write_uint32(0x12345678);

    const PacketStatus status = host_link::client.sendCommand(0x20, data);
    logger.info("%04d: Recieced respond, status = %d, size=%hu", i, status,
                data.size());
    logger.info("Switch = %d", io::SWITCH.read());

    vTaskDelay(500);
  }
}
