#include "crc16.h"

// This algorithm doesn't seem to match anything online, but I couldn't figure out what the difference is.
#define CRC_POLY 0x3D65
uint16_t crc16(uint16_t crc, uint8_t b) 
{
  crc ^= ((uint16_t)b) << 8;
  for (uint8_t i = 0; i < 8; i++) {
    if ((crc & 0x8000) != 0) {
      crc = (crc << 1) ^ CRC_POLY;
    } else {
      crc <<= 1;
    }
  }
  return crc;
}