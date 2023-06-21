#include "serial_packets_client.h"

#include <FreeRTOS.h>
#include <task.h>
#include "static_mutex.h"
#include "time_util.h"

using serial_packets_consts::TYPE_COMMAND;
using serial_packets_consts::TYPE_MESSAGE;
using serial_packets_consts::TYPE_RESPONSE;

PacketStatus SerialPacketsClient::begin(
    Serial& ser, SerialPacketsIncomingCommandHandler command_handler,
    SerialPacketsIncomingMessageHandler message_handler) {
  if (begun()) {
    logger.error("ERROR: Serial packets begin() already called, ignoring.\n");
    return PacketStatus::INVALID_STATE;
  }

  if (!command_handler || !message_handler) {
    logger.error("ERROR: command and message handlers must be non null.\n");
    return PacketStatus::INVALID_ARGUMENT;
  }

  _serial = &ser;
  _command_handler = command_handler;
  _message_handler = message_handler;

  // force_next_pre_flag();
  return PacketStatus::OK;
}

// Decodes and processs incoming packets.
void SerialPacketsClient::rx_task_body() {
  for (;;) {
    if (!begun()) {
      time_util::delay_millis(10);
      continue;
    }

    // NOTE: Blocking. Guarantees n > 0.
    uint16_t n =
        _serial->read(_rx_task_data.in_buffer, sizeof(_rx_task_data.in_buffer));

    for (int i = 0; i < n; i++) {
      const uint8_t b = _rx_task_data.in_buffer[i];
      const bool has_new_packet =
          _rx_task_data.packet_decoder.decode_next_byte(b);

      if (has_new_packet) {
        const PacketType packet_type =
            _rx_task_data.packet_decoder.packet_metadata().packet_type;
        switch (packet_type) {
          case TYPE_COMMAND:
            rx_process_decoded_command_packet(
                _rx_task_data.packet_decoder.packet_metadata().command,
                _rx_task_data.packet_decoder.packet_data());
            break;
          case TYPE_RESPONSE:
            rx_process_decoded_response_packet(
                _rx_task_data.packet_decoder.packet_metadata().response,
                _rx_task_data.packet_decoder.packet_data());
            break;
          case TYPE_MESSAGE:
            rx_process_decoded_message_packet(
                _rx_task_data.packet_decoder.packet_metadata().message,
                _rx_task_data.packet_decoder.packet_data());
            break;
          default:
            logger.error("Unknown incoming packet type: %02hhx", packet_type);
        }
      }
    }
  }
}

// Process an incoming command packet. Called by the rx task only.
void SerialPacketsClient::rx_process_decoded_command_packet(
    const DecodedCommandMetadata& metadata, const SerialPacketsData& data) {
  // This accesses rx task only vars so no need to use _prot_mutex.
  _rx_task_data.tmp_data.clear();
  const uint8_t status = _command_handler(metadata.endpoint, data, _rx_task_data.tmp_data);

  // Send response
  {
    MutexScope mutex_scope(_prot_mutex);

    // const bool insert_pre_flag = prot_check_pre_flag();

    // Encode the packet in wire format.
    if (!_prot.packet_encoder.encode_response_packet(
            metadata.cmd_id, status, _rx_task_data.tmp_data, 
            &_prot.tmp_stuffed_packet)) {
      logger.error("Failed to encode response packet. Dropping.");
      return;
    }

    // Blocking.
    // const uint16_t written =
    _serial->write(_prot.tmp_stuffed_packet._buffer,
                   _prot.tmp_stuffed_packet.size());
  }
}

// Process an incoming response packet. Called by the rx task only.
void SerialPacketsClient::rx_process_decoded_response_packet(
    const DecodedResponseMetadata& metadata, const SerialPacketsData& data) {
  if (!metadata.cmd_id) {
    // NOTE: Potentially blocking.
    logger.error("Incoming response packet has cmd_id = 0.");
    return;
  }
  {
    MutexScope mutex_scope(_prot_mutex);
    CommandContext* context = find_context_by_cmd_id(metadata.cmd_id);
    if (!context) {
      // NOTE: Potentially blocking.
      logger.error(
          "Incoming response packet has no pending command %08lx. May timeout.",
          metadata.cmd_id);
      return;
    }

    context->state = CommandContext::DONE;
    context->data->copy_from(data);
    context->response_status = metadata.status;
  }
}

// Process an incoming message packet. Called from the rx task only.
void SerialPacketsClient::rx_process_decoded_message_packet(
    const DecodedMessageMetadata& metadata, const SerialPacketsData& data) {
  _message_handler(metadata.endpoint, data);
}

PacketStatus SerialPacketsClient::sendCommand(uint8_t endpoint,
                                              SerialPacketsData& data,

                                              uint16_t timeout_millis) {
  if (!begun()) {
    logger.error("Client's begin() was not called");
    return PacketStatus::INVALID_STATE;
  }

  // Verify timeout.
  if (timeout_millis > MAX_CMD_TIMEOUT_MILLIS) {
    // NOTE: This is blocking.
    logger.error("Invalid command timeout %hu ms, should be at most %d ms",
                 timeout_millis, MAX_CMD_TIMEOUT_MILLIS);
    return PacketStatus::OUT_OF_RANGE;
  }

  Elappsed command_timer;
  CommandContext* context = nullptr;
  uint32_t cmd_id = 0;
  {
    MutexScope mutex_scope(_prot_mutex);

    // Find a free command context.
    context = find_free_context();
    if (!context) {
      // NOTE: This is blocking.
      logger.error("Can't send a command, too many commands in progress (%d)",
                   MAX_PENDING_COMMANDS);
      return PacketStatus::TOO_MANY_COMMANDS;
    }

    cmd_id = assign_cmd_id();

    // Determine if to insert a packet flag.
    // const bool insert_pre_flag = prot_check_pre_flag();

    // Encode the packet in wire format.
    if (!_prot.packet_encoder.encode_command_packet(
            cmd_id, endpoint, data, 
            &_prot.tmp_stuffed_packet)) {
      // NOTE: This is blocking.
      logger.error("Failed to encode command packet");
      return PacketStatus::GENERAL_ERROR;
    }

    // Push to TX buffer. It's all or nothing, no partial push.
    const uint16_t size = _prot.tmp_stuffed_packet.size();
    // NOTE: This is blocking.
    _serial->write(_prot.tmp_stuffed_packet._buffer, size);

    // Note: This is blocking.
    logger.verbose("Written a command packet with %hu bytes", size);

    // Set up the cmd context. Setting a non zero cmd_id allocates it.
    context->state = CommandContext::WAITING_FOR_RESPONSE;
    context->cmd_id = cmd_id;
    context->data = &data;
  }

  // NOTE: This is blocking.
  logger.verbose("Command packet written ok, cmd_id = %08lx", cmd_id);

  for (;;) {
    {
      MutexScope mutex_scope(_prot_mutex);

      // No need to find the context by cmd id since it can't
      // be moved.
      if (context->state == CommandContext::IDLE || context->cmd_id != cmd_id) {
        // Should not happen. The context should not be reallocated while we
        // wait for response.
        data.clear();
        return PacketStatus::GENERAL_ERROR;
      }

      if (context->state == CommandContext::DONE) {
        const PacketStatus response_status = context->response_status;
        context->clear();
        return response_status;
      }

      if (command_timer.elapsed_millis() > timeout_millis) {
        logger.warning("Command timeout.");
        data.clear();
        context->clear();
        return PacketStatus::TIMEOUT;
      }
    }

    // TODO: Use an event driven waiting instead.
    time_util::delay_millis(2);
  }
}

PacketStatus SerialPacketsClient::sendMessage(uint8_t endpoint,
                                              const SerialPacketsData& data) {
  if (!begun()) {
    logger.error("Client's begin() was not called");
    return PacketStatus::INVALID_STATE;
  }

  {
    // Wait to grab the mutex.
    MutexScope mutex(_prot_mutex);

    // Determine if to insert a packet flag.
    // const bool insert_pre_flag = prot_check_pre_flag();

    // Encode the packet in wire format.
    if (!_prot.packet_encoder.encode_message_packet(
            endpoint, data,  &_prot.tmp_stuffed_packet)) {
      logger.error("Failed to encode message packet, data_size=%hu",
                   data.size());
      return PacketStatus::GENERAL_ERROR;
    }

    // Push to TX buffer. It's all or nothing, no partial push.
    const uint16_t size = _prot.tmp_stuffed_packet.size();
    // NOTE: We assume that this will not be blocking since we verified
    // the number of avilable bytes in the buffer. We force large buffer
    // using a build flag such as -DSERIAL_TX_BUFFER_SIZE=4096.
    // const uint16_t written =
    _serial->write(_prot.tmp_stuffed_packet._buffer, size);

    logger.verbose("Written a message packet with %hu bytes", size);
    return PacketStatus::OK;
  }
}
