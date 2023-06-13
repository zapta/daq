#include "FreeRTOS.h"
#include "Unity.h"
#include "io.h"
#include "task.h"

void setUp(void) {}

void tearDown(void) {}

void test_xyz() { TEST_ASSERT_TRUE(2 > 1); }

void app_main() {
  // LEt the USB/Serial time to settle down.
  vTaskDelay(3000);

  UNITY_BEGIN();
  RUN_TEST(test_xyz);
  UNITY_END();

  for (;;) {
    vTaskDelay(100);
    io::LED.toggle();
  }
}