// Unit test of error handler module.

#include <unity.h>

#include "../unity_util.h"
#include "error_handler.h"
#include "logger.h"
#include "time_util.h"

void setUp() {}
void tearDown() {}

void test_delay() {
  const uint32_t start_millis = time_util::millis();
  error_handler::signaling_delay(10);
  const uint32_t elpased_millis = time_util::millis() - start_millis;
  // NOTE: Messages is visible only with pio test -vv
  logger.info("test_busy_loop_delay(): elapsed = %lu ms", elpased_millis);
  TEST_ASSERT_GREATER_OR_EQUAL(2500, elpased_millis);
  TEST_ASSERT_LESS_OR_EQUAL(3500, elpased_millis);
}

void app_main() {
  unity_util::common_start();

  // serial::serial1.init();

  UNITY_BEGIN();
  RUN_TEST(test_delay);
  UNITY_END();

  unity_util::common_end();
}
