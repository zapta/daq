#include "unity_util.h"

#include <FreeRTOS.h>
#include <task.h>

#include "io.h"

namespace unity_util {

void common_start() {
  // LEt the USB/Serial time to settle down.
  vTaskDelay(3000);
}

void common_end() {
  for (;;) {
    vTaskDelay(100);
    io::LED.toggle();
  }
}

}  // namespace unity_util