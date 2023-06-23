
#include "serial_packets_test_utils.h"

#include <unity.h>
#include "time_util.h"


void assert_vectors_equal(const std::vector<uint8_t> expected,
                          const std::vector<uint8_t> actual) {
  TEST_ASSERT_EQUAL(expected.size(), actual.size());
  for (uint32_t i = 0; i < expected.size(); i++) {
    TEST_ASSERT_EQUAL_HEX8(expected.at(i), actual.at(i));
  }
}


