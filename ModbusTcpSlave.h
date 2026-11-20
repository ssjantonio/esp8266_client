#pragma once

#include <Arduino.h>
#include <ESPAsyncTCP.h>

// Mapa holding (FC03), familia ESP del poller HQ.ModbusRealtime.
// 0..1  valor float32 IEEE754 big-endian (reg0=hi, reg1=lo) — misma magnitud que TCP 0x32
// 2     código de error
// 3     versión firmware
// 4     sensor_id
// 5     firma 0x4851
// 6     versión de mapa
// 7..14 identifier (16 chars, 2 por registro)
// 15..22 host_name (16 chars, 2 por registro)
#define MODBUS_HOLDING_COUNT 23
#define MODBUS_MAP_SIGNATURE 0x4851
#define MODBUS_MAP_VERSION 2

class ModbusTcpSlave
{
public:
  void begin(uint16_t port = 502);
  void end();
  bool isRunning() const { return _running; }

  void setHolding(uint16_t addr, uint16_t value);
  uint16_t getHolding(uint16_t addr) const;
  void setAsciiBlock(uint16_t startAddr, const char *text, uint8_t maxChars);

  // true después de atender al menos una trama Modbus.
  bool hasServedRequest() const { return _servedRequest; }
  unsigned long lastRequestMs() const { return _lastRequestMs; }

private:
  AsyncServer *_server = nullptr;
  bool _running = false;
  uint16_t _holding[MODBUS_HOLDING_COUNT];
  uint8_t _activeClients = 0;
  AsyncClient *_clients[2] = {nullptr, nullptr};
  bool _servedRequest = false;
  unsigned long _lastRequestMs = 0;

  void forgetClient(AsyncClient *client);

  static void onNewClient(void *arg, AsyncClient *client);
  static void onClientData(void *arg, AsyncClient *client, void *data, size_t len);
  static void onClientDisconnect(void *arg, AsyncClient *client);
  static void onClientError(void *arg, AsyncClient *client, int8_t error);

  void handleRequest(AsyncClient *client, const uint8_t *data, size_t len);
  void sendException(AsyncClient *client, uint16_t tx, uint8_t unit, uint8_t function, uint8_t code);
};

extern ModbusTcpSlave modbusSlave;
