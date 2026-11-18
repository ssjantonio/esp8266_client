#ifndef SENSORDATA_H
#define SENSORDATA_H

#include "Arduino.h"

struct __attribute__((packed, aligned(1))) requestData_t
{
  char headStart; //1
  uint16_t version; //2
  char groupId; //4
  char sensorId; //5
  char sensorType; //6
  char command; //7
  char padding; //8
};

union __attribute__((packed, aligned(1))) TCP_Request_t
{
  requestData_t data;
  char TCPPacket[sizeof(requestData_t)];
};

struct __attribute__((packed, aligned(1))) sensorData_t{
  char headStart; //1     0
  uint16_t version; //2,3       1,2
  char groupId; //4       3
  char sensorId; //5      4
  char sensorType; //6    5
  char command; //7       6
  char padding; //8       7
  uint16_t length; //9-10  8,9
  float value; //10-14    10,11,12,13
  uint16_t crc; //14-16   14,15
};

union __attribute__((packed, aligned(1))) TCP_Packet_t{
 sensorData_t sensor;
 char TCPPacket[sizeof(sensorData_t)];
};

struct __attribute__((packed, aligned(1))) deviceData_t{
  byte headStart; //1           0      
  uint16_t version; //2,3        1,2  
  byte groupId; //4             3
  byte sensorId; //5            4
  byte sensorType; //6          5
  byte command; //7             6
  byte padding; //8             7
  uint16_t length; //8-10       8,9
  char host_name[16]; //10-26
  char identifier[16]; //26-42
  char wifi_ip[16]; //
  uint16_t crc; //42-44
};

union __attribute__((packed, aligned(1))) TCP_Device_t{
 deviceData_t sensor;
 char TCPPacket[sizeof(deviceData_t)];
};


struct __attribute__((packed, aligned(1))) deviceIPConfig_t{
  byte headStart; //1           0      
  uint16_t version; //2,3        1,2  
  byte groupId; //4             3
  byte sensorId; //5            4
  byte sensorType; //6          5
  byte command; //7             6
  byte padding; //8             7
  uint16_t length; //8-10       8,9
  char host_name[16]; //10-26
  char identifier[16]; //26-42
  byte ip[4]; //
  byte gateway[4]; //
  uint16_t crc; //42-44
};

union __attribute__((packed, aligned(1))) TCP_DeviceIP_t{
 deviceIPConfig_t sensor;
 char TCPPacket[sizeof(deviceIPConfig_t)];
};




uint16_t calcTCPPacketCrc(const char*packet, int size);

#define PACKET_SIZE sizeof(sensorData_t)
#define DEVICE_PACKET_SIZE sizeof(deviceData_t)
#endif