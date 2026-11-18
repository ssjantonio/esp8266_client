#include "SensorData.h"

uint16_t calcTCPPacketCrc(const char *byteData, int size)
{
  uint16_t iSum = 0;
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(byteData);
  for (uint16_t i = 0; i < size - 2; i++)
    iSum += bytes[i];

  return (uint16_t)((~iSum) + 1);
}