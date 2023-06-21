

#pragma once

#include "main.h"

#include <vector>

#include "serial_packets_client.h"

// Side affect of reseting the reading.
template <uint16_t N>
void populate_data(SerialPacketsBuffer<N>& data,
                   const std::vector<uint8_t> bytes) {
  data.clear();
  TEST_ASSERT_GREATER_OR_EQUAL(bytes.size(), data.capacity());
  for (uint32_t i = 0; i < bytes.size(); i++) {
    data.write_uint8(bytes.at(i));
    TEST_ASSERT_FALSE(data.had_write_errors());
  }
  TEST_ASSERT_GREATER_OR_EQUAL(bytes.size(), data.size());
}

template <uint16_t N>
void fill_data_uint8(SerialPacketsBuffer<N>& data, uint8_t value, int count) {
  TEST_ASSERT_FALSE(data.had_write_errors());
  for (int i = 0; i < count; i++) {
    data.write_uint8(value);
    TEST_ASSERT_FALSE(data.had_write_errors());
  }
}

template <uint16_t N>
std::vector<uint8_t> copy_data(const SerialPacketsBuffer<N>& data) {
  std::vector<uint8_t> result;
  data.reset_reading();
  while (data.bytes_to_read()) {
    const uint8_t b = data.read_uint8();
    result.push_back(b);
    TEST_ASSERT_FALSE(data.had_read_errors());
  }
  TEST_ASSERT_TRUE(data.all_read_ok());
  return result;
}

void assert_vectors_equal(const std::vector<uint8_t> expected,
                          const std::vector<uint8_t> actual);
                          
template <uint16_t N>
void assert_data_equal(const SerialPacketsBuffer<N>& data,
                        const std::vector<uint8_t> expected) {
  const std::vector<uint8_t> data_vect = copy_data(data);
  assert_vectors_equal(expected, data_vect);
}



class PacketEncoderInspector {
 public:
  PacketEncoderInspector(SerialPacketsEncoder& encoder) : _encoder(encoder) {}

  bool run_byte_stuffing(const EncodedPacketBuffer& in, 
                         StuffedPacketBuffer* out) {
    return _encoder.byte_stuffing(in,  out);
  }

 private:
  SerialPacketsEncoder& _encoder;
};

class PacketDecoderInspector {
 public:
  PacketDecoderInspector(const SerialPacketsDecoder& decoder)
      : _decoder(decoder) {}

  uint16_t packet_len() { return _decoder._packet_len; }
  bool in_packet() { return _decoder._in_packet; }
  bool pending_escape() { return _decoder._pending_escape; };

 private:
  const SerialPacketsDecoder& _decoder;
};


