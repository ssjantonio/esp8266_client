#include "ModbusTcpSlave.h"

ModbusTcpSlave modbusSlave;

void ModbusTcpSlave::begin(uint16_t port)
{
  if (_running)
    return;

  memset(_holding, 0, sizeof(_holding));
  _holding[5] = MODBUS_MAP_SIGNATURE;
  _holding[6] = MODBUS_MAP_VERSION;
  _activeClients = 0;

  _server = new AsyncServer(port);
  _server->onClient(&ModbusTcpSlave::onNewClient, this);
  _server->begin();
  _running = true;
}

void ModbusTcpSlave::end()
{
  if (!_running)
    return;
  if (_server)
  {
    _server->end();
    delete _server;
    _server = nullptr;
  }
  _running = false;
  _activeClients = 0;
}

void ModbusTcpSlave::setHolding(uint16_t addr, uint16_t value)
{
  if (addr < MODBUS_HOLDING_COUNT)
    _holding[addr] = value;
}

uint16_t ModbusTcpSlave::getHolding(uint16_t addr) const
{
  if (addr < MODBUS_HOLDING_COUNT)
    return _holding[addr];
  return 0;
}

void ModbusTcpSlave::setAsciiBlock(uint16_t startAddr, const char *text, uint8_t maxChars)
{
  if (!text)
    text = "";
  for (uint8_t i = 0; i < maxChars; i += 2)
  {
    uint16_t addr = startAddr + (i / 2);
    if (addr >= MODBUS_HOLDING_COUNT)
      break;
    char c0 = (i < maxChars) ? text[i] : 0;
    char c1 = (i + 1 < maxChars) ? text[i + 1] : 0;
    if (c0 == 0)
    {
      for (uint16_t z = addr; z < (uint16_t)(startAddr + (maxChars / 2)) && z < MODBUS_HOLDING_COUNT; z++)
        _holding[z] = 0;
      break;
    }
    _holding[addr] = (uint16_t)(((uint8_t)c0 << 8) | (uint8_t)c1);
  }
}

void ModbusTcpSlave::onNewClient(void *arg, AsyncClient *client)
{
  ModbusTcpSlave *self = reinterpret_cast<ModbusTcpSlave *>(arg);
  if (!self || !client)
    return;

  // Limitar conexiones para no agotar heap del ESP8266.
  if (self->_activeClients >= 2)
  {
    client->close(true);
    return;
  }

  self->_activeClients++;
  client->setNoDelay(true);
  client->onData(&ModbusTcpSlave::onClientData, self);
  client->onDisconnect(&ModbusTcpSlave::onClientDisconnect, self);
  client->onError(&ModbusTcpSlave::onClientError, self);
}

void ModbusTcpSlave::onClientDisconnect(void *arg, AsyncClient *client)
{
  (void)client;
  ModbusTcpSlave *self = reinterpret_cast<ModbusTcpSlave *>(arg);
  if (self && self->_activeClients > 0)
    self->_activeClients--;
}

void ModbusTcpSlave::onClientError(void *arg, AsyncClient *client, int8_t error)
{
  (void)arg;
  (void)error;
  // onDisconnect descuenta el cliente. Descontar aquí también deja el cupo en cero
  // y el siguiente poller ya no entra.
  if (client)
    client->close(true);
}

void ModbusTcpSlave::onClientData(void *arg, AsyncClient *client, void *data, size_t len)
{
  ModbusTcpSlave *self = reinterpret_cast<ModbusTcpSlave *>(arg);
  if (!self || !client || !data || len == 0)
    return;
  self->handleRequest(client, reinterpret_cast<const uint8_t *>(data), len);
}

void ModbusTcpSlave::sendException(AsyncClient *client, uint16_t tx, uint8_t unit, uint8_t function, uint8_t code)
{
  uint8_t resp[9];
  resp[0] = (tx >> 8) & 0xFF;
  resp[1] = tx & 0xFF;
  resp[2] = 0;
  resp[3] = 0;
  resp[4] = 0;
  resp[5] = 3;
  resp[6] = unit;
  resp[7] = (uint8_t)(function | 0x80);
  resp[8] = code;
  if (client->space() >= sizeof(resp) && client->canSend())
  {
    client->add(reinterpret_cast<const char *>(resp), sizeof(resp));
    client->send();
  }
}

void ModbusTcpSlave::handleRequest(AsyncClient *client, const uint8_t *data, size_t len)
{
  // Petición FC03 típica = 12 bytes. Si llegan varios frames, procesar de a uno.
  size_t offset = 0;
  while (len - offset >= 12)
  {
    const uint8_t *req = data + offset;
    uint16_t tx = (uint16_t)((req[0] << 8) | req[1]);
    uint16_t proto = (uint16_t)((req[2] << 8) | req[3]);
    uint16_t pduLen = (uint16_t)((req[4] << 8) | req[5]);
    uint8_t unit = req[6];
    uint8_t function = req[7];

    if (proto != 0 || pduLen < 6)
    {
      offset += 12;
      continue;
    }

    size_t frameLen = 6 + pduLen;
    if (len - offset < frameLen)
      break;

    if (function != 0x03)
    {
      sendException(client, tx, unit, function, 0x01); // illegal function
      offset += frameLen;
      continue;
    }

    uint16_t address = (uint16_t)((req[8] << 8) | req[9]);
    uint16_t count = (uint16_t)((req[10] << 8) | req[11]);
    if (count < 1 || count > 125 || (uint32_t)address + count > MODBUS_HOLDING_COUNT)
    {
      sendException(client, tx, unit, function, 0x02); // illegal data address
      offset += frameLen;
      continue;
    }

    uint8_t byteCount = (uint8_t)(count * 2);
    // MBAP length = bytes after length field = unit + FC + byteCount + data
    uint16_t mbapLen = (uint16_t)(1 + 2 + byteCount);

    uint8_t resp[7 + 250];
    if (7 + byteCount > sizeof(resp))
    {
      sendException(client, tx, unit, function, 0x03);
      offset += frameLen;
      continue;
    }

    resp[0] = (tx >> 8) & 0xFF;
    resp[1] = tx & 0xFF;
    resp[2] = 0;
    resp[3] = 0;
    resp[4] = (mbapLen >> 8) & 0xFF;
    resp[5] = mbapLen & 0xFF;
    resp[6] = unit;
    resp[7] = 0x03;
    resp[8] = byteCount;
    for (uint16_t i = 0; i < count; i++)
    {
      uint16_t v = _holding[address + i];
      resp[9 + i * 2] = (v >> 8) & 0xFF;
      resp[10 + i * 2] = v & 0xFF;
    }

    size_t outLen = 9 + byteCount;
    if (client->space() >= outLen && client->canSend())
    {
      client->add(reinterpret_cast<const char *>(resp), outLen);
      client->send();
    }

    offset += frameLen;
  }
}
