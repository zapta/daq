// Unit test of the packet encoder function.

#include <FreeRTOS.h>
#include <task.h>
#include <unity.h>

#include <memory>
#include <vector>

#include "../../unity_util.h"
#include "../serial_packets_test_utils.h"
#include "serial_packets_client.h"
#include "static_task.h"
#include "time_util.h"

static Serial& DATA_SERIAL = serial::serial1;

struct Command {
  uint32_t time_millis;
  uint8_t endpoint;
  std::vector<uint8_t> data;
};

struct Message {
  uint32_t time_millis;
  uint8_t endpoint;
  std::vector<uint8_t> data;
};

static std::vector<Command> command_list;
static std::vector<Message> message_list;

// Contains a fake response that the test setup for the command
// handler to return.
struct FakeResponse {
  void clear() { set(PacketStatus::OK, {}, 0); }
  void set(PacketStatus new_status, const std::vector<uint8_t>& new_data,
           uint16_t new_delay) {
    status = new_status;
    data.clear();
    data.insert(data.end(), new_data.begin(), new_data.end());
    delay = new_delay;
  }
  PacketStatus status;
  std::vector<uint8_t> data;
  uint16_t delay;
};

static FakeResponse fake_response;

PacketStatus command_handler(uint8_t endpoint, const SerialPacketsData& data,
                             SerialPacketsData& response_data) {
  // Record the incoming command.
  Command item;
  item.time_millis = time_util::millis();
  item.endpoint = endpoint;
  item.data = copy_data(data);
  command_list.push_back(item);
  // Return a requested fake response.
  populate_data(response_data, fake_response.data);
  if (fake_response.delay) {
    time_util::delay_millis(fake_response.delay);
  }
  return fake_response.status;
}

void message_handler(uint8_t endpoint, const SerialPacketsData& data) {
  Message item;
  item.time_millis = time_util::millis();
  item.endpoint = endpoint;
  item.data = copy_data(data);
  message_list.push_back(item);
}

static std::unique_ptr<SerialPacketsClient> client;

// This buffer can be large so we avoid allocating it on the stack.
static SerialPacketsData packet_data;

void rx_task_body(void* argument) {
  // Should not return.
  client->rx_task_body();
  Error_Handler();
}

static StaticTask<2000> rx_task(rx_task_body, "rx_test", 5);

void setUp() {
  rx_task.stop();
  packet_data.clear();
  client.reset();
  client = std::make_unique<SerialPacketsClient>();

  // Clear serial input
  time_util::delay_millis(100);
  uint8_t byte_buffer[1];
  while (DATA_SERIAL.read(byte_buffer, 1, false)) {
  }

  PacketStatus status =
      client->begin(DATA_SERIAL, command_handler, message_handler);
  TEST_ASSERT_EQUAL(PacketStatus::OK, status);
  fake_response.clear();
  command_list.clear();
  message_list.clear();
}

void tearDown() {}

// Simple test that the data link is looped for the tests.
void test_simple_serial_loop() {
  // Send a string.
  TEST_ASSERT_EQUAL(0, DATA_SERIAL.available());
  const char str[] = "xyz";
  DATA_SERIAL.write_str(str);

  // Verify it was looped back.
  time_util::delay_millis(100);
  TEST_ASSERT_GREATER_OR_EQUAL(3, DATA_SERIAL.available());
  for (int i = 0; i < 3; i++) {
    uint8_t b;
    const uint16_t n = DATA_SERIAL.read(&b, 1, false);
    TEST_ASSERT_EQUAL(1, n);
    TEST_ASSERT_EQUAL_HEX8(str[i], b);
  }
}

void test_send_message_loop() {
  rx_task.start();

  const std::vector<uint8_t> data = {0x11, 0x22, 0x33};
  populate_data(packet_data, data);
  TEST_ASSERT_EQUAL(PacketStatus::OK, client->sendMessage(0x20, packet_data));

  time_util::delay_millis(200);
  TEST_ASSERT_EQUAL(0, command_list.size());
  TEST_ASSERT_EQUAL(1, message_list.size());
  const Message& message = message_list.at(0);
  TEST_ASSERT_EQUAL(0x20, message.endpoint);
  assert_vectors_equal(data, message.data);
}

void test_send_command_loop() {
  rx_task.start();

  const std::vector<uint8_t> data = {0x11, 0x22, 0x33};
  populate_data(packet_data, data);
  fake_response.set((PacketStatus)0x99, {0xaa, 0xbb, 0xcc}, 0);

  const PacketStatus status = client->sendCommand(0x20, packet_data, 1000);
  // We get back the fake response status we requested above.
  TEST_ASSERT_EQUAL(0x99, status);

  time_util::delay_millis(200);
  TEST_ASSERT_EQUAL(1, command_list.size());
  TEST_ASSERT_EQUAL(0, message_list.size());
  TEST_ASSERT_EQUAL(0, client->num_pending_commands());
  const Command& command = command_list.at(0);
  TEST_ASSERT_EQUAL_HEX8(0x20, command.endpoint);
  assert_vectors_equal(data, command.data);

  assert_data_equal(packet_data, {0xaa, 0xbb, 0xcc});
}

// Inject delay to the response and test for timeout status.
void test_command_timeout() {
  rx_task.start();

  const std::vector<uint8_t> data = {0x11, 0x22, 0x33};
  populate_data(packet_data, data);
  // Response will be delayed by 500ms.
  fake_response.set(PacketStatus::OK, {0xaa, 0xbb, 0xcc}, 500);
  Elappsed timer;
  TEST_ASSERT_EQUAL(PacketStatus::TIMEOUT,
                    client->sendCommand(0x20, packet_data, 200));
  const uint32_t time_millis = timer.elapsed_millis();
  TEST_ASSERT_GREATER_OR_EQUAL(200, time_millis);
  TEST_ASSERT_LESS_OR_EQUAL(250, time_millis);

  // The client should call the response handler with TIMEOUT status.
  TEST_ASSERT_EQUAL(1, command_list.size());
  // TEST_ASSERT_EQUAL(1, response_list.size());
  TEST_ASSERT_EQUAL(0, message_list.size());
  TEST_ASSERT_EQUAL(0, client->num_pending_commands());
  // Timeout error returns empty data.
  assert_data_equal(packet_data, {});
}

void app_main() {
  unity_util::common_start();

  serial::serial1.init();

  UNITY_BEGIN();

  RUN_TEST(test_simple_serial_loop);
  RUN_TEST(test_send_message_loop);
  RUN_TEST(test_send_command_loop);
  RUN_TEST(test_command_timeout);

  UNITY_END();

  unity_util::common_end();
}
