// Unit test of the time utils.

#include <FreeRTOS.h>
#include <task.h>
#include <unity.h>

#include "../../unity_util.h"
#include "time_util.h"

void setUp() {}
void tearDown() {}

// Time util assumes that tick rate is 1ms.
void test_tick_rate() { TEST_ASSERT_EQUAL(1000, configTICK_RATE_HZ); }

void test_delay_0ms() {
  TEST_ASSERT_EQUAL(1000, configTICK_RATE_HZ);
  const uint32_t start = xTaskGetTickCount();
  time_util::delay_millis(0);
  const uint32_t elapsed = xTaskGetTickCount() - start;
  // We allow up to 1ms slack, in case the test has a race condition.
  TEST_ASSERT_LESS_OR_EQUAL(1, elapsed);
}

void test_delay_100ms() {
  TEST_ASSERT_EQUAL(1000, configTICK_RATE_HZ);
  const uint32_t start = xTaskGetTickCount();
  time_util::delay_millis(100);
  const uint32_t elapsed = xTaskGetTickCount() - start;
  TEST_ASSERT_GREATER_OR_EQUAL(100, elapsed);
  TEST_ASSERT_LESS_OR_EQUAL(101, elapsed);
}

void test_elapsed() {
  Elappsed timer;
  TEST_ASSERT_LESS_OR_EQUAL(1, timer.elapsed_millis());
  time_util::delay_millis(100);
  TEST_ASSERT_GREATER_OR_EQUAL(100, timer.elapsed_millis());
  TEST_ASSERT_LESS_OR_EQUAL(101, timer.elapsed_millis());
}

void test_set_elapsed() {
  Elappsed timer;
  TEST_ASSERT_LESS_OR_EQUAL(1, timer.elapsed_millis());
  timer.set(300);
  TEST_ASSERT_GREATER_OR_EQUAL(300, timer.elapsed_millis());
  TEST_ASSERT_LESS_OR_EQUAL(301, timer.elapsed_millis());
}

void test_reset_elapsed() {
  Elappsed timer;
  time_util::delay_millis(100);
  TEST_ASSERT_GREATER_OR_EQUAL(100, timer.elapsed_millis());
  TEST_ASSERT_LESS_OR_EQUAL(101, timer.elapsed_millis());
  timer.reset();
  TEST_ASSERT_LESS_OR_EQUAL(1, timer.elapsed_millis());
}

void app_main() {
  unity_util::common_start();

  UNITY_BEGIN();
  RUN_TEST(test_tick_rate);
  RUN_TEST(test_delay_0ms);
  RUN_TEST(test_delay_100ms);
  RUN_TEST(test_elapsed);
  RUN_TEST(test_set_elapsed);
  RUN_TEST(test_reset_elapsed);
  UNITY_END();

  unity_util::common_end();
}
