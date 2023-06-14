#include "unity_util.h"

#include <FreeRTOS.h>
#include <task.h>

#include "io.h"
#include "time_util.h"

namespace unity_util {

void common_start() {
  // LEt the USB/Serial time to settle down.
  time_util::delay_millis(3000);
}

void common_end() {
  for (;;) {
    time_util::delay_millis(100);
    io::LED.toggle();
  }
}

}  // namespace unity_util