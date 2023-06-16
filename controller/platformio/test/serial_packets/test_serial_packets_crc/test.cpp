// Unit test of the packet crc function.

#include <unity.h>

#include "../../unity_util.h"
#include "../serial_packets_test_utils.h"
#include "serial_packets_crc.h"

void setUp() {}
void tearDown() {}

void test_empty_data() {
  const uint8_t data[] = {};
  const uint16_t crc = serial_packets_gen_crc16(data, sizeof(data));
  TEST_ASSERT_EQUAL(0xffff, crc);
}

// Per https://srecord.sourceforge.net/crc16-ccitt.html#results this is
// the wrong result, but it's a common mistake, and compatible with the
// the Python library we use.
void test_data1() {
  const uint8_t data[] = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39};
  const uint16_t crc = serial_packets_gen_crc16(data, sizeof(data));
  TEST_ASSERT_EQUAL_HEX32(0x29b1, crc);
}

void test_data2() {
  const uint8_t data[] = {0x01, 0x00, 0x00, 0x00, 0x07, 0x14,
                          0xc8, 0x00, 0x00, 0x04, 0xd2};
  const uint16_t crc = serial_packets_gen_crc16(data, sizeof(data));
  TEST_ASSERT_EQUAL_HEX32(0x1f49, crc);
}

void app_main() {
  unity_util::common_start();

  UNITY_BEGIN();
  RUN_TEST(test_empty_data);
  RUN_TEST(test_data1);
  RUN_TEST(test_data2);
  UNITY_END();

  unity_util::common_end();
}