#include <Arduino.h>

#include "io.h"
#include "serial_packets_client.h"

// A callback for all incoming commands. Configured once during initialization.
void command_handler(byte endpoint, const SerialPacketsData& data,
                     byte& response_status, SerialPacketsData& response_data) {
  // Typically dispatch to sub-handler by endpoint.
  Serial.printf("Command Handler called, endpoint=%02x, data=%hu\n", endpoint,
                data.size());
  // Command response to send back
  response_status = OK;
  response_data.write_uint8(0x12);
  response_data.write_uint16(0x3456);
}

// A callback for all incoming messages. Configured once during initialization.
void message_handler(byte endpoint, const SerialPacketsData& data) {
  // Typically dispatch to sub-handler by endpoint.
  Serial.printf("Message Handler called, endpoint=%02x, data=%hu\n", endpoint,
                data.size());
  // Parse the command data.
  const uint8_t v1 = data.read_uint8();
  const uint32_t v2 = data.read_uint32();
  Serial.printf("v1=%02x, v2=%08x, ok=%s\n", v1, v2,
                data.all_read_ok() ? "yes" : "no");
}

// A callback to handle responses of outgoing commands. Configured per
// command, when calling sendCommand().
void response_handler(uint32_t cmd_id, byte response_status,
                      const SerialPacketsData& response_data,
                      uint32_t user_data) {
  Serial.printf(
      "Response Handler called, cmd_id=%08x, status=%hu, size=%hu, "
      "user_data=%08x\n",
      cmd_id, response_status, response_data.size(), user_data);
}

// The serial Packets client.
static SerialPacketsClient packets(command_handler, message_handler);

void dump() {
  // RCC_PLL1DIVR
  {
    const uint32_t w = RCC->PLL1DIVR;
    const int divn1 = (w >> 0) & 0x1ff;
    const int divn1_val = 1 + divn1;
    const int divp1 = (w >> 9) & 0x7f;
    const int divp1_val = 1 + divp1;
    const int divq1 = (w >> 16) & 0x7f;
    const int divq1_val = 1 + divq1;
    const int divr1 = (w >> 24) & 0x7f;
    const int divr1_val = 1 + divr1;
    Serial.printf("PLL1DIVR %08x: divn1=%d, divp1=%d, divq1=%d, divr1=%d%\n", w,
                  divn1_val, divp1_val, divq1_val, divr1_val);
  }
  // RCC_D1CFGR
  {
    const uint32_t w = RCC->D1CFGR;
    const int hpre = ((w >> 0) & 0b1111);
    const int hpre_val = (hpre & 0b1000) ? (1 << (1 + (hpre & 0b111))) : 1;
    const int d1dpre = 1 + ((w >> 4) & 0b111);
    const int d1dpre_val = (d1dpre & 0b100) ? (1 << (1 + (d1dpre & 0b11))) : 1;
    const int d1cpre = 1 + ((w >> 8) & 0b1111);
    const int d1cpre_val =
        (d1cpre & 0b1000) ? (1 << (1 + (d1cpre & 0b111))) : 1;
    Serial.printf("D1CFGR %08x: hpre=%d, d1dpre=%d, d1cpre=%d\n", w, hpre_val,
                  d1dpre_val, d1cpre_val);
  }
  // RCC_PLLCKSELR
  {
    // TODO: Parse all fields.
    const uint32_t w = RCC->PLLCKSELR;
    const int divm1 = ((w >> 4) & 0b111111);
    const int divm1_val = divm1;
    Serial.printf("PLLCKSELR %08x: divm1=%d\n", w, divm1_val);
  }
}

void setup() {
  // SysClkHalfSpeed();
  // SysClkFullSpeed();

  io::setup();

  // A serial port for packet data communication.
  Serial2.begin(115200);

  // A serial port for debug log.
  Serial.begin(115200);

  // Start the packets client.
  packets.setLogLevel(SERIAL_PACKETS_LOG_INFO);
  packets.begin(Serial2, Serial);
}

static uint32_t last_send_time_millis = 0;
static uint32_t test_cmd_id = 0;
// These buffers can be large, depending on the configuration
// so we avoid allocating them on the stack.
static SerialPacketsData test_packet_data;

void loop() {
  // Service serial packets loop. Callbacks are called within
  // this call.
  packets.loop();

  // Periodically send a test command and a message.
  if (millis() - last_send_time_millis > 1000) {
    last_send_time_millis = millis();
    io::LED.toggle();

    //dump();

    // Send a command
    test_packet_data.clear();
    test_packet_data.write_uint8(0x10);
    test_packet_data.write_uint32(millis());

    if (!packets.sendCommand(0x20, test_packet_data, 0xaabbccdd,
                             response_handler, test_cmd_id, 1000)) {
      Serial.println("sendCommand() failed");
    }

    // Send a message
    test_packet_data.clear();
    test_packet_data.write_uint8(0x10);
    test_packet_data.write_uint32(millis());
    if (!packets.sendMessage(0x30, test_packet_data)) {
      Serial.println("sendMessage() failed");
    }
  }
}
