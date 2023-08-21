#include "Unity.h"
#include "time_util.h"
#include "gpio_pins.h"

void setUp() {}

void tearDown() {}

void test_sample() { TEST_ASSERT_TRUE(true); }

void app_main() {
  // LEt the USB/Serial time to settle down.
  time_util::delay_millis(3000);

  UNITY_BEGIN();
  RUN_TEST(test_sample);
  UNITY_END();

  for (;;) {
    time_util::delay_millis(100);
    gpio_pins::LED.toggle();
  }
}