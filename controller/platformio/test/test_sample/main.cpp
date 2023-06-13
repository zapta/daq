#include "FreeRTOS.h"
#include "Unity.h"
#include "io.h"
#include "task.h"
#include "../unity_util.h"

void setUp(void) {}

void tearDown(void) {}

void test_sample() { TEST_ASSERT_TRUE(2 > 1); }

void app_main() {
  unity_util::common_start();

  UNITY_BEGIN();
  RUN_TEST(test_sample);
  UNITY_END();

  unity_util::common_end();
}