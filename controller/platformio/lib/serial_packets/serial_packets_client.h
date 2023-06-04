// The main APi of the Serial Packets library package.

// https://docs.arduino.cc/learn/contributions/arduino-library-style-guide

#pragma once

// #include <Arduino.h>
#include <inttypes.h>

#include "Serial.h"
#include "serial_packets_consts.h"
#include "serial_packets_data.h"
#include "serial_packets_decoder.h"
#include "serial_packets_encoder.h"
#include "serial_packets_logger.h"
#include "serial_packets_timer.h"

// We limit the command timeout duration to avoid accomulation of
// pending commands.
constexpr uint16_t MAX_CMD_TIMEOUT_MILLIS = 10000;
constexpr uint16_t DEFAULT_CMD_TIMEOUT_MILLIS = 1000;

// Define status codes of command responses.


// A callback type for all incoming commands. Handler should
// set response_status and response_data with the response
// info.
typedef void (*SerialPacketsIncomingCommandHandler)(
    uint8_t endpoint, const SerialPacketsData& command_data,
    uint8_t& response_status, SerialPacketsData& response_data);

// A callback type for incoming command response.
// typedef void (*SerialPacketsCommandResponseHandler)(
//     uint32_t cmd_id, uint8_t response_status,
//     const SerialPacketsData& response_data, uint32_t user_data);

// A callback type for incoming messages.
typedef void (*SerialPacketsIncomingMessageHandler)(
    uint8_t endpoint, const SerialPacketsData& message_data);

// Constructor.
class SerialPacketsClient {
 public:
  SerialPacketsClient() {}
  //     SerialPacketsIncomingCommandHandler command_handler,
  //     SerialPacketsIncomingMessageHandler message_handler )
  //     :  
  //       _optional_command_handler(command_handler),
  //       _optional_message_handler(message_handler)

  // {}

  // SerialPacketsClient() = delete;

  // Initialize the client with a serial stream for data
  // communication and an optional serial stream for debug
  // log.
  // void begin(Stream& data_stream, Stream& log_stream);
  void begin(Serial& ser, SerialPacketsIncomingCommandHandler command_handler,
      SerialPacketsIncomingMessageHandler message_handler);

  // This method should be called frequently from the main
  // loop() of the program. Program should be non blocking
  // (avoid delay()) such that this method is called frequently.
  // It process incoming data, invokes the callback handlers
  // and cleans up timeout commands.
  // void loop();
  void rx_task_body();

  // Adjust log level. If
  // void setLogLevel(SerialPacketsLogLevel level) {
  //   _logger.set_level(level);
  // }

  // Send a command to given endpoint and with given data. Use the
  // provided response_handler to pass the command response or
  // timeout information. Returns true if the command was sent.
  PacketStatus sendCommand(
      uint8_t endpoint,  SerialPacketsData& data,
      //  uint32_t user_data,
      //  SerialPacketsCommandResponseHandler response_handler,
      //  uint32_t& cmd_id,
      uint16_t timeout_millis = DEFAULT_CMD_TIMEOUT_MILLIS);

  // Send a message to given endpoint and with given data. Returns
  // true if the message was sent. There is not positive verification
  // that the message was actually recieved at the other side. For
  // this, use a command instead.
  bool sendMessage(uint8_t endpoint, const SerialPacketsData& data);

  // Returns the number of in progress commands that wait for a
  // response or to timeout. The max number of allowed pending
  // messages is configurable.
  int num_pending_commands() {
    int count = 0;
    for (int i = 0; i < MAX_PENDING_COMMANDS; i++) {
      if (_prot.command_contexts[i].cmd_id) {
        count++;
      }
    }
    return count;
  }

 private:
  // For testing.
  friend class SerialPacketsClientInspector;
  bool _ignore_rx_for_testing = false;

  // Contains the information of a single pending command.
  struct CommandContext {
    CommandContext() { clear(); }

    void clear() {
      state = IDLE;
      cmd_id = 0;
      data = nullptr;
      // expiration_time_millis = 0;
      response_status = PacketStatus::OK;

      // response_handler = nullptr;
      // expiration_time_millis = 0;
      // user_data = 0;
    }

    enum State { IDLE = 0, WAITING_FOR_RESPONSE, DONE };

    State state;

    // Valid in states WAITING_FOR_RESPOSNE and DONE.
    uint32_t cmd_id;
    // Valid in states WAITING_FOR_RESPONSE, and DONE.
    SerialPacketsData* data;
    // Valid in states WAITING_FOR_RESPOSNE and DONE.
    // uint32_t expiration_time_millis;
    // Valid in state DONE.
    PacketStatus response_status;

    // uint32_t user_data;
  };

  // Logger for diagnostics messages.
  // SerialPacketsLogger _logger;

  // User provided command handler. Non null if begun();
  SerialPacketsIncomingCommandHandler  _command_handler = nullptr;
  // user provided message handler. Non null.
  SerialPacketsIncomingMessageHandler  _message_handler = nullptr;

  Serial* _serial = nullptr;

  struct ProtectedState {
    // SerialPacketsData tmp_data;
    StuffedPacketBuffer tmp_stuffed_packet;
    SerialPacketsEncoder packet_encoder;
    // Used to assign command ids. Wraparound is ok. Skipping zero values.
    uint32_t cmd_id_counter = 0;
    // Used to insert pre packet flag byte when packates are sparse.
    SerialPacketsTimer pre_flag_timer;
    // Used to periodically clean pending commands that timeout.
    SerialPacketsTimer cleanup_timer;
    // A table that contains information about pending commands.
    CommandContext command_contexts[MAX_PENDING_COMMANDS];

  };

  // All accesses are protected by _prot_mutex.
  ProtectedState _prot;

  SemaphoreHandle_t _prot_mutex = xSemaphoreCreateMutex();

  // Data that is accessed only by the RX task and thus doesn't 
  // need protection.
  struct RxTaskData { 
    uint8_t in_buffer[50];
    // uint16_t in_buffer_size = 0;
    SerialPacketsDecoder packet_decoder;
    SerialPacketsData tmp_data;
  };

  RxTaskData _rx_task_data;

  // Returns true if begun already called.
  inline bool begun() { return _serial != nullptr; }

  // Assign a fresh command id. Guaranteed to be non zero.
  // Wrap arounds are OK since we clea up timeout commands.
  inline uint32_t assign_cmd_id() {
    _prot.cmd_id_counter++;
    if (_prot.cmd_id_counter == 0) {
      _prot.cmd_id_counter++;
    }
    return _prot.cmd_id_counter;
  }

  // Methos that used to process incoming packets that were
  // decoder by the packet decoder.
  void rx_process_decoded_response_packet(const DecodedResponseMetadata& metadata,
                                       const SerialPacketsData& data);
  void rx_process_decoded_command_packet(const DecodedCommandMetadata& metadata,
                                      const SerialPacketsData& data);
  void rx_process_decoded_message_packet(const DecodedMessageMetadata& metadata,
                                      const SerialPacketsData& data);

  // Manipulate the pre flag timer to force a pre packet flag byte
  // before next packet. Invoked when when we detect errors on the line.
  void force_next_pre_flag() {
    _prot.pre_flag_timer.set(serial_packets_consts::PRE_FLAG_TIMEOUT_MILLIS +
                             1);
  }

  // Subfunctionalties of loop().
  void loop_rx();
  void loop_cleanup();

  // Lookup a non idle context entry with given command id.
  CommandContext* find_context_by_cmd_id(uint32_t cmd_id) {
    for (int i = 0; i < MAX_PENDING_COMMANDS; i++) {
      CommandContext* p = &_prot.command_contexts[i];
      if (p->cmd_id == cmd_id && p->state != CommandContext::IDLE) {
        return p;
      }
    }
    return nullptr;
  }

    CommandContext* find_free_context() {
    for (int i = 0; i < MAX_PENDING_COMMANDS; i++) {
      CommandContext* p = &_prot.command_contexts[i];
      if (p->state == CommandContext::IDLE) {
        return p;
      }
    }
    return nullptr;
  }

  // Determine if the interval from previous packet is large enough that
  // warrants the insertion of a flag byte beofore next packet.
  bool prot_check_pre_flag() {
    // _data_stream->flush();
    const bool result = _prot.pre_flag_timer.elapsed_millis() >
                        serial_packets_consts::PRE_FLAG_TIMEOUT_MILLIS;
    _prot.pre_flag_timer.reset();
    return result;
  }
};
