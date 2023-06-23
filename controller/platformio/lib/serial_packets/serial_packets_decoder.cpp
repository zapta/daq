
#include "serial_packets_decoder.h"

#include "logger.h"
#include "serial_packets_consts.h"
#include "serial_packets_crc.h"

using serial_packets_consts::MAX_PACKET_LEN;
using serial_packets_consts::MIN_PACKET_LEN;
using serial_packets_consts::PACKET_END_FLAG;
using serial_packets_consts::PACKET_ESC;
using serial_packets_consts::PACKET_START_FLAG;
using serial_packets_consts::TYPE_COMMAND;
using serial_packets_consts::TYPE_RESPONSE;
using serial_packets_consts::TYPE_MESSAGE;
using serial_packets_consts::TYPE_LOG;


bool SerialPacketsDecoder::decode_next_byte(uint8_t b) {
  // When not in packet, wait for next  flag byte.
  if (!_in_packet) {
    if (b == PACKET_START_FLAG) {
      reset_packet(true);
      // _in_packet = true;
      // _packet_len = 0;
      // _pending_escape = false;
    } else {
      // TODO: Count and report skipped bytes.
      // Should not happen in normal operation.
    }
    return false;
  }

  // Here _in_packet = true.
  // Handle premature start flag.
  if (b == PACKET_START_FLAG) {
    logger.error("Premature start flag.");
    reset_packet(true);

    // _in_packet = true;
    // _packet_len = 0;
    // _pending_escape = false;
    return false;
  }

  // Handle end of packet flag byte.
  if (b == PACKET_END_FLAG) {
    const bool has_new_packet = process_packet();
    reset_packet(false);
    // _in_packet = false;
    // _packet_len = 0;
    // _pending_escape = false;
    return has_new_packet;
  }

  // Check for size overrun. At this point, we know that the packet will
  // have at least one more  byte.
  if (_packet_len >= MAX_PACKET_LEN) {
    reset_packet(false);
    // _in_packet = false;
    // _packet_len = 0;
    // _pending_escape = false;
    logger.error("Decoded packet overrun");
    return false;
  }

  // Handle escaped byte.
  if (_pending_escape) {
    // Flip the bit per HDLC conventions.
    const uint8_t b1 = b ^ 0x20;
    if (b1 != PACKET_START_FLAG && b1 != PACKET_END_FLAG && b1 != PACKET_ESC) {
      logger.error("Decoded packet has the byte %02hx after an escape byte",
                   b1);
      reset_packet(false);
      // _in_packet = false;
      // _packet_len = 0;
      // _pending_escape = false;
      return false;
    }
    _packet_buffer[_packet_len++] = b1;
    _pending_escape = false;
    return false;
  }

  // Handle escape byte.
  // Here _pending_escape is false.
  if (b == PACKET_ESC) {
    _pending_escape = true;
    return false;
  }

  _packet_buffer[_packet_len++] = b;
  return false;
}

// Returns true if a new packet is available.
bool SerialPacketsDecoder::process_packet() {
  // This is normal in packets that insert pre packet flags.
  // if (!_packet_len) {
  //   return false;
  // }

  if (_pending_escape) {
    logger.error("Packet has a pending escape. Dropping.");
    return false;
  }

  if (_packet_len < MIN_PACKET_LEN) {
    logger.error("Decoded packet is too short: %hu", _packet_len);
    return false;
  }

  // Check CRC. These are the last two bytes in big endian oder.
  const uint16_t packet_crc = decode_uint16_at_index(_packet_len - 2);
  const uint16_t computed_crc =
      serial_packets_gen_crc16(_packet_buffer, _packet_len - 2);
  if (packet_crc != computed_crc) {
    // Serial.printf("crc: %04hx vs %04hx\n", packet_crc, computed_crc);
    logger.error("Decoded packet has bad CRC: %04hx vs %04hx", packet_crc,
                 computed_crc);
    return false;
  }

  // Construct the decoded packet by its type.
  const uint8_t packet_type = _packet_buffer[0];

  // Decode a command packet.
  if (packet_type == TYPE_COMMAND) {
    if (_packet_len < 8) {
      logger.error("Decoded command packet is too short: %hu", _packet_len);
      return false;
    }
    _decoded_metadata.packet_type = TYPE_COMMAND;
    _decoded_metadata.command.cmd_id = decode_uint32_at_index(1);
    _decoded_metadata.command.endpoint = _packet_buffer[5];
    _decoded_data.clear();
    _decoded_data.write_bytes(&_packet_buffer[6], _packet_len - 8);
    return true;
  }

  // Decode a response packet.
  if (packet_type == TYPE_RESPONSE) {
    if (_packet_len < 8) {
      logger.error("Decoded response packet is too short: %hu", _packet_len);
      return false;
    }
    _decoded_metadata.packet_type = TYPE_RESPONSE;
    _decoded_metadata.response.cmd_id = decode_uint32_at_index(1);
    _decoded_metadata.response.status =
        static_cast<PacketStatus>(_packet_buffer[5]);
    _decoded_data.clear();
    _decoded_data.write_bytes(&_packet_buffer[6], _packet_len - 8);
    return true;
  }

  // Decode a message packet.
  if (packet_type == TYPE_MESSAGE) {
    if (_packet_len < 4) {
      logger.error("Decoded message packet is too short: %hu", _packet_len);
      return false;
    }
    _decoded_metadata.packet_type = TYPE_MESSAGE;
    _decoded_metadata.message.endpoint = _packet_buffer[1];
    _decoded_data.clear();
    _decoded_data.write_bytes(&_packet_buffer[2], _packet_len - 4);
    return true;
  }

    // Decode a log packet.
  if (packet_type == TYPE_LOG) {
    if (_packet_len < 3) {
      logger.error("Decoded log packet is too short: %hu", _packet_len);
      return false;
    }
    _decoded_metadata.packet_type = TYPE_LOG;
    // _decoded_metadata.message.endpoint = _packet_buffer[1];
    _decoded_data.clear();
    _decoded_data.write_bytes(&_packet_buffer[1], _packet_len - 3);
    return true;
  }

  logger.error("Decoded packet has an invalid type: %hu", packet_type);
  return false;
}
