// Unit test of the packet decoder function.

#include <unity.h>

#include <memory>
#include <vector>

#include "../../unity_util.h"
#include "../serial_packets_test_utils.h"
#include "serial_packets_decoder.h"

// static std::unique_ptr<SerialPacketsLogger> logger;
static std::unique_ptr<SerialPacketsDecoder> decoder;
static std::unique_ptr<PacketDecoderInspector> inspector;

void setUp() {
  inspector.reset();
  decoder.reset();
  decoder = std::make_unique<SerialPacketsDecoder>();
  inspector = std::make_unique<PacketDecoderInspector>(*decoder);
}

void tearDown() {}

// Helper to to decode a list bytes with expected decoder returned statuses.
static void check_dedoding(SerialPacketsDecoder& decoder,
                           const std::vector<uint8_t>& bytes,
                           const std::vector<bool>& expected_statuses) {
  TEST_ASSERT_EQUAL(bytes.size(), expected_statuses.size());
  for (uint32_t i = 0; i < (uint32_t)bytes.size(); i++) {
    const bool packet_available = decoder.decode_next_byte(bytes.at(i));
    TEST_ASSERT_EQUAL(expected_statuses.at(i), packet_available);
  }
}

void test_initial_state() {
  TEST_ASSERT_EQUAL(0, inspector->packet_len());
  TEST_ASSERT_FALSE(inspector->in_packet());
  TEST_ASSERT_FALSE(inspector->pending_escape());
}

// CRC set to 0x9999 instead of 0x5d99
void test_bad_crc() {
  check_dedoding(*decoder,
                 {0x7c, 0xff, 0x00, 0x7d, 0x5c, 0x22, 0x7d, 0x5e, 0x22, 0x7d,
                  0x99, 0x99, 0x7e},
                 {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
  TEST_ASSERT_EQUAL(0, inspector->packet_len());
  TEST_ASSERT_FALSE(inspector->in_packet());
  TEST_ASSERT_FALSE(inspector->pending_escape());
}

void test_command_decoding() {
  check_dedoding(
      *decoder,
      {0x7c, 0x01, 0xff, 0x12, 0x34, 0x56, 0x20, 0xff, 0x00, 0x7d, 0x5c,
       0x11, 0x7d, 0x5e, 0x22, 0x7d, 0x5d, 0x99, 0x7a, 0xa7, 0x7e},
      {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1});
  TEST_ASSERT_EQUAL(0, inspector->packet_len());
  TEST_ASSERT_FALSE(inspector->in_packet());
  TEST_ASSERT_FALSE(inspector->pending_escape());
  TEST_ASSERT_EQUAL(0x01, decoder->packet_metadata().packet_type);
  TEST_ASSERT_EQUAL_HEX32(0xff123456,
                          decoder->packet_metadata().command.cmd_id);
  TEST_ASSERT_EQUAL_HEX8(0x20, decoder->packet_metadata().command.endpoint);
  assert_data_equal(decoder->packet_data(),
                    {0xff, 0x00, 0x7c, 0x11, 0x7e, 0x22, 0x7d, 0x99});
}

void test_response_decoding() {
  check_dedoding(
      *decoder,
      {0x7c, 0x02, 0xff, 0x12, 0x34, 0x56, 0x20, 0xff, 0x00, 0x7d, 0x5c,
       0x11, 0x7d, 0x5e, 0x22, 0x7d, 0x5d, 0x99, 0xf7, 0x04, 0x7e},
      {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1});
  TEST_ASSERT_EQUAL(0, inspector->packet_len());
  TEST_ASSERT_FALSE(inspector->in_packet());
  TEST_ASSERT_FALSE(inspector->pending_escape());
  TEST_ASSERT_EQUAL(0x02, decoder->packet_metadata().packet_type);
  TEST_ASSERT_EQUAL_HEX32(0xff123456,
                          decoder->packet_metadata().response.cmd_id);
  TEST_ASSERT_EQUAL_HEX8(0x20, decoder->packet_metadata().response.status);
  assert_data_equal(decoder->packet_data(),
                    {0xff, 0x00, 0x7c, 0x11, 0x7e, 0x22, 0x7d, 0x99});
}

void test_message_decoding() {
  check_dedoding(*decoder,
                 {0x7c, 0x03, 0x20, 0xff, 0x00, 0x7d, 0x5c, 0x11, 0x7d, 0x5e,
                  0x22, 0x7d, 0x5d, 0x99, 0xe7, 0x2d, 0x7e},
                 {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1});
  TEST_ASSERT_EQUAL(0, inspector->packet_len());
  TEST_ASSERT_FALSE(inspector->in_packet());
  TEST_ASSERT_FALSE(inspector->pending_escape());
  TEST_ASSERT_EQUAL(0x03, decoder->packet_metadata().packet_type);
  TEST_ASSERT_EQUAL(0x20, decoder->packet_metadata().message.endpoint);
  assert_data_equal(decoder->packet_data(),
                    {0xff, 0x00, 0x7c, 0x11, 0x7e, 0x22, 0x7d, 0x99});
}

void test_log_decoding() {
  check_dedoding(*decoder,
                 {0x7c, 0x04, 0xff, 0x00, 0x7d, 0x5c, 0x11, 0x7d,
                          0x5e, 0x22, 0x7d, 0x5d, 0x99, 0x94, 0xba, 0x7e},
                 {0, 0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1});
  TEST_ASSERT_EQUAL(0, inspector->packet_len());
  TEST_ASSERT_FALSE(inspector->in_packet());
  TEST_ASSERT_FALSE(inspector->pending_escape());
  TEST_ASSERT_EQUAL(0x04, decoder->packet_metadata().packet_type);
  assert_data_equal(decoder->packet_data(),
                    {0xff, 0x00, 0x7c, 0x11, 0x7e, 0x22, 0x7d, 0x99});
}

void app_main() {
  unity_util::common_start();

  UNITY_BEGIN();

  RUN_TEST(test_initial_state);
  RUN_TEST(test_bad_crc);
  RUN_TEST(test_command_decoding);
  RUN_TEST(test_response_decoding);
  RUN_TEST(test_message_decoding);
  RUN_TEST(test_log_decoding);

  UNITY_END();

  unity_util::common_end();
}
