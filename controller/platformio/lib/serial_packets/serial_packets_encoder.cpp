#include "serial_packets_encoder.h"

#include "serial_packets_consts.h"

using serial_packets_consts::PACKET_END_FLAG;
using serial_packets_consts::PACKET_ESC;
using serial_packets_consts::PACKET_START_FLAG;
using serial_packets_consts::TYPE_COMMAND;
using serial_packets_consts::TYPE_MESSAGE;
using serial_packets_consts::TYPE_RESPONSE;

bool SerialPacketsEncoder::byte_stuffing(const EncodedPacketBuffer& in,
                                         StuffedPacketBuffer* out) {
  out->clear();
  const uint16_t capacity = out->capacity();
  uint16_t j = 0;

  // If requested, prepend a flag byte.
  // if (insert_pre_flag) {
  if (j + 1 >= capacity) {
    logger.error("Insufficient space when trying to byte stuff a packet (#1).");
    return false;
  }
  out->_buffer[j++] = PACKET_START_FLAG;
  // }

  // Byte stuff the in bytes.
  for (uint16_t i = 0; i < in._size; i++) {
    const uint8_t b = in._buffer[i];
    if (b == PACKET_START_FLAG || b == PACKET_END_FLAG || b == PACKET_ESC) {
      if (j + 2 >= capacity) {
        logger.error(
            "Insufficient space when trying to byte stuff a packet "
            "(#2).");
        return false;
      }
      out->_buffer[j++] = PACKET_ESC;
      out->_buffer[j++] = b ^ 0x20;
    } else {
      if (j + 1 >= capacity) {
        logger.error(
            "Insufficient space when trying to byte stuff a packet (#3).");
        return false;
      }
      out->_buffer[j++] = b;
    }
  }

  // Append post flag.
  if (j + 1 >= capacity) {
    logger.error("Insufficient space when trying to byte stuff a packet (#4).");
    return false;
  }
  out->_buffer[j++] = PACKET_END_FLAG;

  // Update outptut data size.
  out->_size = j;
  return true;
}

bool SerialPacketsEncoder::encode_command_packet(uint32_t cmd_id,
                                                 uint8_t endpoint,
                                                 const SerialPacketsData& data,
                                                 StuffedPacketBuffer* out) {
  // Encode packet in _tmp_data.
  _tmp_data.clear();
  _tmp_data.write_uint8(TYPE_COMMAND);
  _tmp_data.write_uint32(cmd_id);
  _tmp_data.write_uint8(endpoint);
  _tmp_data.write_bytes(data._buffer, data._size);
  _tmp_data.write_uint16(_tmp_data.crc16());
  if (_tmp_data.had_write_errors()) {
    logger.error("Error encoding a command packet. Data size: %hu",
                 data.size());
    return false;
  }

  // Byte stuffed into the outupt data.
  return byte_stuffing(_tmp_data, out);
}

bool SerialPacketsEncoder::encode_response_packet(uint32_t cmd_id,
                                                  uint8_t status,
                                                  const SerialPacketsData& data,
                                                  StuffedPacketBuffer* out) {
  // Encode packet in _tmp_data.
  _tmp_data.clear();
  _tmp_data.write_uint8(TYPE_RESPONSE);
  _tmp_data.write_uint32(cmd_id);
  _tmp_data.write_uint8(status);
  _tmp_data.write_bytes(data._buffer, data._size);
  _tmp_data.write_uint16(_tmp_data.crc16());
  if (_tmp_data.had_write_errors()) {
    logger.error("Error encoding a response packet. Data size: %hu",
                 data.size());
    return false;
  }

  // Byte stuffed into the outupt data.
  return byte_stuffing(_tmp_data, out);
}

bool SerialPacketsEncoder::encode_message_packet(uint8_t endpoint,
                                                 const SerialPacketsData& data,
                                                 StuffedPacketBuffer* out) {
  // Encode packet in _tmp_data.
  _tmp_data.clear();
  _tmp_data.write_uint8(TYPE_MESSAGE);
  _tmp_data.write_uint8(endpoint);
  _tmp_data.write_bytes(data._buffer, data._size);
  _tmp_data.write_uint16(_tmp_data.crc16());
  if (_tmp_data.had_write_errors()) {
    logger.error("Error encoding a response packet. Data size: %hu",
                 data.size());
    return false;
  }

  // Byte stuffed into the outupt data.
  return byte_stuffing(_tmp_data, out);
}
