#pragma once
#define AUTO_UPDATE_H
#include <ESP8266httpUpdate.h>
#include <ESP8266HTTPClient.h>
#include <flash_hal.h>
#include "OtaFsLayout.h"

// Solo Logf: evitar incluir SerialLog.h aquí (orden de tipos IPAddress/String en el IDE).
void Logf(const char *fmt, ...);

// Tiene que ser exactamente la versión con la que se sube el .bin en /arduino-upload.
// Si el texto de aquí es menor que el registro del servidor, el equipo se reprograma en bucle.
const char * TRUC_VERSION = "1_1_0";
const char * SPIFFS_VERSION = "0_1_0";

const char * updateUrl = "http://192.168.17.159/update/esp8266";

/** Telemetría para que updServer resuelva fs_ota_layout (1M64K512K, 4M2M1M, …). */
static inline void addOtaLayoutHeaders(HTTPClient &http)
{
  http.addHeader(F("x-ESP8266-chip-size"), String(ESP.getFlashChipRealSize()));
  http.addHeader(F("x-ESP8266-free-space"), String(ESP.getFreeSketchSpace()));
  http.addHeader(F("x-ESP8266-fs-partition-bytes"), String(FS_PHYS_SIZE));
  const char *layoutId = currentOtaFsLayoutId();
  if (layoutId)
    http.addHeader(F("x-ESP8266-fs-ota-layout"), layoutId);
}

// 200 = hay .bin nuevo. 304 = no hay. <0 = no hubo respuesta.
// No baja el bin: así el chequeo no tiene que cortar Modbus.
inline int probeSketchUpdate()
{
  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, updateUrl))
    return -1;
  http.setTimeout(8000);
  http.setUserAgent(F("ESP8266-http-Update"));
  http.addHeader(F("x-ESP8266-mode"), F("sketch"));
  http.addHeader(F("x-ESP8266-version"), TRUC_VERSION);
  http.addHeader(F("x-ESP8266-action"), F("check"));
  addOtaLayoutHeaders(http);
  int code = http.GET();
  http.end();
  return code;
}

inline t_httpUpdate_return actualUpdate(bool sketch = false)
{
  WiFiClient client;
  ESPhttpUpdate.rebootOnUpdate(false);
  ESPhttpUpdate.setClientTimeout(20000);

  Logf("Find updates on %s\n", updateUrl);

  if (sketch) {
    HTTPClient http;
    if (!http.begin(client, updateUrl))
      return HTTP_UPDATE_FAILED;
    addOtaLayoutHeaders(http);
    return ESPhttpUpdate.update(http, TRUC_VERSION);
  }
  return ESPhttpUpdate.updateFS(client, updateUrl, SPIFFS_VERSION);
}