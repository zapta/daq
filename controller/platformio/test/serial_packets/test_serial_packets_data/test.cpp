// Unit test of the packet data class.

#include <unity.h>

#include <vector>

#include "../../unity_util.h"
#include "../serial_packets_test_utils.h"
#include "serial_packets_data.h"

// These data buffer can be large so we avoid having them on the stack.
static SerialPacketsData d1;
static SerialPacketsData d2;

void setUp() {
  d1.clear();
  d2.clear();
}

void tearDown() {}

void test_constructor() {
  TEST_ASSERT_EQUAL(MAX_PACKET_DATA_LEN, d1.capacity());
  TEST_ASSERT_EQUAL(0, d1.size());
  TEST_ASSERT_EQUAL(0, d1.bytes_read());
  TEST_ASSERT_EQUAL(0, d1.bytes_to_read());
  TEST_ASSERT_TRUE(d1.all_read());
  TEST_ASSERT_FALSE(d1.had_read_errors());
  TEST_ASSERT_FALSE(d1.had_write_errors());
  TEST_ASSERT_TRUE(d1.all_read_ok());
}

void test_copy_from() {
  d1.write_uint32(0x12345);
  d1.read_uint8();
  d1.read_uint32();
  TEST_ASSERT_EQUAL(1, d1.bytes_read());
  TEST_ASSERT_TRUE(d1.had_read_errors());
  d2.write_uint16(0x2233);
  d1.copy_from(d2);
  TEST_ASSERT_EQUAL(2, d1.size());
  TEST_ASSERT_EQUAL(2, d2.size());
  assert_data_equal(d1, {0x22, 0x33});
  assert_data_equal(d2, {0x22, 0x33});
  // Reading state got cleared.
  TEST_ASSERT_EQUAL(0, d1.bytes_read());
  TEST_ASSERT_FALSE(d1.had_read_errors());
}

void test_write_uint8() {
  d1.write_uint8(0x02);
  TEST_ASSERT_FALSE(d1.had_read_errors());
  TEST_ASSERT_EQUAL(1, d1.size());
  TEST_ASSERT_EQUAL(0, d1.bytes_read());
  TEST_ASSERT_EQUAL(1, d1.bytes_to_read());
  assert_data_equal(d1, {0x02});
}

void test_write_uint8_new_error() {
  fill_data_uint8(d1, 0x11, d1.capacity());
  TEST_ASSERT_TRUE(d1.is_full());
  d1.write_uint8(0x02);
  TEST_ASSERT_TRUE(d1.had_write_errors());
  TEST_ASSERT_EQUAL(d1.capacity(), d1.size());
}

void test_write_uint16() {
  populate_data(d1, {0x11, 0x22});
  d1.write_uint16(0x3344);
  TEST_ASSERT_EQUAL(4, d1.size());
  TEST_ASSERT_EQUAL(0, d1.bytes_read());
  TEST_ASSERT_EQUAL(4, d1.bytes_to_read());
  assert_data_equal(d1, {0x11, 0x22, 0x33, 0x44});
}

void test_write_uint16_new_error() {
  fill_data_uint8(d1, 0x11, d1.capacity() - 1);
  TEST_ASSERT_FALSE(d1.had_read_errors());
  TEST_ASSERT_EQUAL(1, d1.free_bytes());
  d1.write_uint16(0x8899);
  TEST_ASSERT_TRUE(d1.had_write_errors());
  TEST_ASSERT_EQUAL(1, d1.free_bytes());
}

void test_write_uint32() {
  populate_data(d1, {0x11, 0x22});
  d1.write_uint32(0x33445566);
  TEST_ASSERT_EQUAL(6, d1.size());
  TEST_ASSERT_EQUAL(0, d1.bytes_read());
  TEST_ASSERT_EQUAL(6, d1.bytes_to_read());
  assert_data_equal(d1, {0x11, 0x22, 0x33, 0x44, 0x55, 0x66});
}

void test_write_uint32_new_error() {
  fill_data_uint8(d1, 0x11, d1.capacity() - 3);
  TEST_ASSERT_FALSE(d1.had_read_errors());
  TEST_ASSERT_EQUAL(3, d1.free_bytes());
  d1.write_uint32(0x8899aabb);
  TEST_ASSERT_TRUE(d1.had_write_errors());
  TEST_ASSERT_EQUAL(3, d1.free_bytes());
}

void test_write_bytes() {
  populate_data(d1, {0x11, 0x22});
  const uint8_t bytes[] = {0x33, 0x44, 0x55};
  d1.write_bytes(bytes, sizeof(bytes));
  TEST_ASSERT_EQUAL(5, d1.size());
  TEST_ASSERT_EQUAL(0, d1.bytes_read());
  TEST_ASSERT_EQUAL(5, d1.bytes_to_read());
  assert_data_equal(d1, {0x11, 0x22, 0x33, 0x44, 0x55});
}

void test_write_bytes_new_error() {
  fill_data_uint8(d1, 0x11, d1.capacity() - 2);
  TEST_ASSERT_FALSE(d1.had_read_errors());
  TEST_ASSERT_EQUAL(2, d1.free_bytes());
  const uint8_t bytes[] = {0x33, 0x44, 0x55};
  d1.write_bytes(bytes, sizeof(bytes));
  TEST_ASSERT_TRUE(d1.had_write_errors());
  TEST_ASSERT_EQUAL(2, d1.free_bytes());
}

void test_read_uint8() {
  // SerialPacketsData d1(20);
  populate_data(d1, {0x11, 0x22, 0x33});
  const uint8_t v1 = d1.read_uint8();
  const uint8_t v2 = d1.read_uint8();
  TEST_ASSERT_FALSE(d1.had_read_errors());
  TEST_ASSERT_EQUAL_HEX8(v1, 0x11);
  TEST_ASSERT_EQUAL_HEX8(v2, 0x22);
  TEST_ASSERT_EQUAL(2, d1.bytes_read());
  TEST_ASSERT_EQUAL(1, d1.bytes_to_read());
}

void test_read_uint8_new_error() {
  const uint8_t v = d1.read_uint8();
  TEST_ASSERT_TRUE(d1.had_read_errors());
  TEST_ASSERT_EQUAL_HEX8(v, 0x00);
  TEST_ASSERT_EQUAL(0, d1.bytes_read());
  TEST_ASSERT_EQUAL(0, d1.bytes_to_read());
}

void test_read_uint16() {
  populate_data(d1, {0x11, 0x22, 0x33});
  const uint16_t v = d1.read_uint16();
  TEST_ASSERT_FALSE(d1.had_read_errors());
  TEST_ASSERT_EQUAL_HEX16(v, 0x1122);
  TEST_ASSERT_EQUAL(2, d1.bytes_read());
  TEST_ASSERT_EQUAL(1, d1.bytes_to_read());
}

void test_read_uint16_new_error() {
  populate_data(d1, {0x11});
  const uint16_t v = d1.read_uint16();
  TEST_ASSERT_TRUE(d1.had_read_errors());
  TEST_ASSERT_EQUAL_HEX16(v, 0);
  TEST_ASSERT_EQUAL(0, d1.bytes_read());
  TEST_ASSERT_EQUAL(1, d1.bytes_to_read());
}

void test_read_uint32() {
  populate_data(d1, {0x11, 0x22, 0x33, 0x44, 0x55});
  const uint32_t v = d1.read_uint32();
  TEST_ASSERT_FALSE(d1.had_read_errors());
  TEST_ASSERT_EQUAL_HEX32(v, 0x11223344);
  TEST_ASSERT_EQUAL(4, d1.bytes_read());
  TEST_ASSERT_EQUAL(1, d1.bytes_to_read());
}

void test_read_uint32_new_error() {
  populate_data(d1, {0x11, 0x22, 0x33});
  const uint32_t v = d1.read_uint32();
  TEST_ASSERT_TRUE(d1.had_read_errors());
  TEST_ASSERT_EQUAL_HEX32(v, 0x00);
  TEST_ASSERT_EQUAL(0, d1.bytes_read());
  TEST_ASSERT_EQUAL(3, d1.bytes_to_read());
}

void test_read_bytes() {
  populate_data(d1, {0x11, 0x22, 0x33, 0x44, 0x55});
  uint8_t actual[] = {0x99, 0x99, 0x99, 0x99};
  // We read into the second and third bytes.
  d1.read_bytes(actual + 1, 2);
  TEST_ASSERT_FALSE(d1.had_read_errors());
  const uint8_t expected[] = {0x99, 0x11, 0x22, 0x99};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, actual, 4);
  TEST_ASSERT_EQUAL(2, d1.bytes_read());
  TEST_ASSERT_EQUAL(3, d1.bytes_to_read());
}

void test_read_bytes_new_error() {
  populate_data(d1, {0x11, 0x22});
  uint8_t actual[] = {0x11, 0x22, 0x33};
  d1.read_bytes(actual, sizeof(actual));
  TEST_ASSERT_TRUE(d1.had_read_errors());
  const uint8_t expected[] = {0x00, 0x00, 0x00};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, actual, 3);
  TEST_ASSERT_EQUAL(0, d1.bytes_read());
  TEST_ASSERT_EQUAL(2, d1.bytes_to_read());
}

void app_main() {
  unity_util::common_start();

  UNITY_BEGIN();
  RUN_TEST(test_constructor);

  // Writing
  RUN_TEST(test_write_uint8);
  RUN_TEST(test_write_uint8_new_error);
  RUN_TEST(test_write_uint16);
  RUN_TEST(test_write_uint16_new_error);
  RUN_TEST(test_write_uint32);
  RUN_TEST(test_write_uint32_new_error);
  RUN_TEST(test_write_bytes);
  RUN_TEST(test_write_bytes_new_error);

  // Reading
  RUN_TEST(test_read_uint8);
  RUN_TEST(test_read_uint8_new_error);
  RUN_TEST(test_read_uint16);
  RUN_TEST(test_read_uint16_new_error);
  RUN_TEST(test_read_uint32);
  RUN_TEST(test_read_uint32_new_error);
  RUN_TEST(test_read_bytes);
  RUN_TEST(test_read_bytes_new_error);

  UNITY_END();

  unity_util::common_end();
}
