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

void test_byte_sutffing() {
  populate_data(in, {0xff, 0x00, 0x7c, 0x22, 0x7e, 0x22, 0x7d, 0x99});
  TEST_ASSERT_TRUE(inspector->run_byte_stuffing(in, &out));
  assert_data_equal(out, {0x7c, 0xff, 0x00, 0x7d, 0x5c, 0x22, 0x7d, 0x5e, 0x22,
                          0x7d, 0x5d, 0x99, 0x7e});
}

void test_encode_command_packet() {
  populate_data(data, {0xff, 0x00, 0x7c, 0x11, 0x7e, 0x22, 0x7d, 0x99});

  TEST_ASSERT_TRUE(
      encoder->encode_command_packet(0xff123456, 0x20, data, &out));

  assert_data_equal(
      out, {0x7c, 0x01, 0xff, 0x12, 0x34, 0x56, 0x20, 0xff, 0x00, 0x7d, 0x5c,
            0x11, 0x7d, 0x5e, 0x22, 0x7d, 0x5d, 0x99, 0x7a, 0xa7, 0x7e});
}

void test_encode_response_packet() {
  populate_data(data, {0xff, 0x00, 0x7c, 0x11, 0x7e, 0x22, 0x7d, 0x99});

  TEST_ASSERT_TRUE(
      encoder->encode_response_packet(0xff123456, 0x20, data, &out));

  assert_data_equal(
      out, {0x7c, 0x02, 0xff, 0x12, 0x34, 0x56, 0x20, 0xff, 0x00, 0x7d, 0x5c,
            0x11, 0x7d, 0x5e, 0x22, 0x7d, 0x5d, 0x99, 0xf7, 0x04, 0x7e});
}

void test_encode_message_packet() {
  populate_data(data, {0xff, 0x00, 0x7c, 0x11, 0x7e, 0x22, 0x7d, 0x99});

  TEST_ASSERT_TRUE(encoder->encode_message_packet(0x20, data, &out));

  assert_data_equal(out, {0x7c, 0x03, 0x20, 0xff, 0x00, 0x7d, 0x5c, 0x11, 0x7d,
                          0x5e, 0x22, 0x7d, 0x5d, 0x99, 0xe7, 0x2d, 0x7e});
}

void app_main() {
  unity_util::common_start();

  UNITY_BEGIN();

  RUN_TEST(test_byte_sutffing);
  RUN_TEST(test_encode_command_packet);
  RUN_TEST(test_encode_response_packet);
  RUN_TEST(test_encode_message_packet);

  UNITY_END();

  unity_util::common_end();
}
