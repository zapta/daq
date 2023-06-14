// Packet CRC function.

#pragma once

#include <inttypes.h>

uint16_t serial_packets_gen_crc16(const uint8_t *data, int size,
                                  uint16_t initial_crc = 0xffff);
