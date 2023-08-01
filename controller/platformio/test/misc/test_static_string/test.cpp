// Unit test of the StaticString class.

// #include <FreeRTOS.h>
// #include <task.h>
#include <unity.h>

#include "../../unity_util.h"
#include "static_string.h"

void setUp() {}
void tearDown() {}

// Time util assumes that tick rate is 1ms.
// void test_tick_rate() { TEST_ASSERT_EQUAL(1000, configTICK_RATE_HZ); }

void test_default_constructor() {
  StaticString<3> str;
  TEST_ASSERT_EQUAL(0, str.kMaxLen);
  TEST_ASSERT_EQUAL(0, str.max_len());
  TEST_ASSERT_EQUAL(0, str.len());
  TEST_ASSERT_EQUAL(0, strlen(str.c_str()));
  TEST_ASSERT_TRUE(str.is_empty());
  TEST_ASSERT_FALSE(str.is_full());
}

void test_set_c_str() {
  StaticString<2> str;
  TEST_ASSERT_TRUE(str.set_c_str("xy"));
  TEST_ASSERT_EQUAL(2, str.len());
  TEST_ASSERT_EQ(0, strcmp("xy", str.c_str()));
  // String too long.
  TEST_ASSERT_FALSE(str.set_c_str("xyz"));
  TEST_ASSERT_EQUAL(2, str.len());
  TEST_ASSERT_EQ(0, strcmp("xy", str.c_str()));
}

void test_append() {
  StaticString<2> str;
  TEST_ASSERT_TRUE(str.append('x'));
  TEST_ASSERT_EQ(0, strcmp("x", str.c_str()));

  TEST_ASSERT_TRUE(str.append('y'));
  TEST_ASSERT_EQ(0, strcmp("xy", str.c_str()));

  TEST_ASSERT_FALSE(str.append('z'));
  TEST_ASSERT_EQ(0, strcmp("xy", str.c_str()));
}

void app_main() {
  unity_util::common_start();

  UNITY_BEGIN();
  RUN_TEST(test_default_constructor);
  RUN_TEST(test_set_c_str);
  RUN_TEST(test_append);
  UNITY_END();

  unity_util::common_end();
}
