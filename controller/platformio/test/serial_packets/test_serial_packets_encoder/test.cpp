// Unit test of the packet encoder function.

#include <unity.h>

#include <memory>
#include <vector>

#include "../../unity_util.h"
#include "../serial_packets_test_utils.h"
#include "serial_packets_encoder.h"

static std::unique_ptr<SerialPacketsEncoder> encoder;
static std::unique_ptr<PacketEncoderInspector> inspector;

// These can be large so we don't want to allocate them on stack.
static SerialPacketsData data;
static EncodedPacketBuffer in;
static StuffedPacketBuffer out;

void setUp(void) {
  in.clear();
  out.clear();
  inspector.reset();
  encoder.reset();
  encoder = std::make_unique<SerialPacketsEncoder>();
  inspector = std::make_unique<PacketEncoderInspector>(*encoder);
}

void tearDown() {}

void test_byte_sutffing_with_pre_flag() {
  populate_data(in, {0xff, 0x00, 0x7e, 0x22, 0x7d, 0x99});
  TEST_ASSERT_TRUE(inspector->run_byte_stuffing(in, true, &out));
  assert_data_equal(
      out, {0x7e, 0xff, 0x00, 0x7d, 0x5e, 0x22, 0x7d, 0x5d, 0x99, 0x7e});
}

void test_byte_sutffing_without_pre_flag() {
  populate_data(in, {0xff, 0x00, 0x7e, 0x22, 0x7d, 0x99});
  TEST_ASSERT_TRUE(inspector->run_byte_stuffing(in, false, &out));
  assert_data_equal(out,
                    {0xff, 0x00, 0x7d, 0x5e, 0x22, 0x7d, 0x5d, 0x99, 0x7e});
}

void test_encode_command_packet_with_pre_flag() {
  populate_data(data, {0xff, 0x00, 0x7e, 0x22, 0x7d, 0x99});

  TEST_ASSERT_TRUE(
      encoder->encode_command_packet(0x12345678, 0x20, data, true, &out));

  assert_data_equal(
      out, {0x7e, 0x01, 0x12, 0x34, 0x56, 0x78, 0x20, 0xff, 0x00, 0x7d, 0x5e,
            0x22, 0x7d, 0x5d, 0x99, 0xD4, 0x80, 0x7e});
}

void test_encode_command_packet_without_pre_flag() {
  populate_data(data, {0xff, 0x00, 0x7e, 0x22, 0x7d, 0x99});

  TEST_ASSERT_TRUE(
      encoder->encode_command_packet(0x12345678, 0x20, data, false, &out));

  assert_data_equal(out, {0x01, 0x12, 0x34, 0x56, 0x78, 0x20, 0xff, 0x00, 0x7d,
                          0x5e, 0x22, 0x7d, 0x5d, 0x99, 0xD4, 0x80, 0x7e});
}

void test_encode_response_packet_with_pre_flag() {
  populate_data(data, {0xff, 0x00, 0x7e, 0x22, 0x7d, 0x99});

  TEST_ASSERT_TRUE(
      encoder->encode_response_packet(0x12345678, 0x20, data, true, &out));

  assert_data_equal(
      out, {0x7e, 0x02, 0x12, 0x34, 0x56, 0x78, 0x20, 0xff, 0x00, 0x7d, 0x5e,
            0x22, 0x7d, 0x5d, 0x99, 0xd1, 0x1f, 0x7e});
}

void test_encode_response_packet_without_pre_flag() {
  populate_data(data, {0xff, 0x00, 0x7e, 0x22, 0x7d, 0x99});

  TEST_ASSERT_TRUE(
      encoder->encode_response_packet(0x12345678, 0x20, data, false, &out));

  assert_data_equal(out, {0x02, 0x12, 0x34, 0x56, 0x78, 0x20, 0xff, 0x00, 0x7d,
                          0x5e, 0x22, 0x7d, 0x5d, 0x99, 0xd1, 0x1f, 0x7e});
}

void test_encode_message_packet_with_pre_flag() {
  populate_data(data, {0xff, 0x00, 0x7e, 0x22, 0x7d, 0x99});

  TEST_ASSERT_TRUE(encoder->encode_message_packet(0x20, data, true, &out));

  assert_data_equal(out, {0x7e, 0x03, 0x20, 0xff, 0x00, 0x7d, 0x5e, 0x22, 0x7d,
                          0x5d, 0x99, 0xa7, 0x1e, 0x7e});
}

void test_encode_message_packet_without_pre_flag() {
  populate_data(data, {0xff, 0x00, 0x7e, 0x22, 0x7d, 0x99});

  TEST_ASSERT_TRUE(encoder->encode_message_packet(0x20, data, false, &out));

  assert_data_equal(out, {0x03, 0x20, 0xff, 0x00, 0x7d, 0x5e, 0x22, 0x7d, 0x5d,
                          0x99, 0xa7, 0x1e, 0x7e});
}

void app_main() {
  unity_util::common_start();

  UNITY_BEGIN();

  RUN_TEST(test_byte_sutffing_with_pre_flag);
  RUN_TEST(test_byte_sutffing_without_pre_flag);
  RUN_TEST(test_encode_command_packet_with_pre_flag);
  RUN_TEST(test_encode_command_packet_without_pre_flag);
  RUN_TEST(test_encode_response_packet_with_pre_flag);
  RUN_TEST(test_encode_response_packet_without_pre_flag);
  RUN_TEST(test_encode_message_packet_with_pre_flag);
  RUN_TEST(test_encode_message_packet_without_pre_flag);

  UNITY_END();

  unity_util::common_end();
}
