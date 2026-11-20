#pragma once
#define AUTO_UPDATE_H
#include <ESP8266httpUpdate.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <cstring>
#include <flash_hal.h>
#include "OtaFsLayout.h"

void Logf(const char *fmt, ...);

const char * TRUC_VERSION = "1_1_0";
const char * SPIFFS_VERSION = "0_1_0";

const char * updateUrl = "http://192.168.17.159/update/esp8266";

/** Último release resuelto por el servidor (probe check o descarga). */
struct OtaReleaseInfo {
  bool valid;
  char version[20];
  char release_layout[32];
  char client_layout[32];
  char release_name[48];
  char match_kind[20];
  char binary_file[48];
  int release_id;
  int binary_bytes;
};

extern OtaReleaseInfo g_otaReleaseInfo;

static inline void clearOtaReleaseInfo()
{
  memset(&g_otaReleaseInfo, 0, sizeof(g_otaReleaseInfo));
}

static inline void logOtaReleaseInfo(const char *prefix)
{
  if (!g_otaReleaseInfo.valid)
  {
    Logf("%s: (sin metadatos de release)\n", prefix);
    return;
  }
  Logf(
      "%s: v=%s id=%d layout=%s client=%s match=%s name=%s bytes=%d file=%s\n",
      prefix, g_otaReleaseInfo.version, g_otaReleaseInfo.release_id,
      g_otaReleaseInfo.release_layout, g_otaReleaseInfo.client_layout,
      g_otaReleaseInfo.match_kind, g_otaReleaseInfo.release_name,
      g_otaReleaseInfo.binary_bytes, g_otaReleaseInfo.binary_file);
}

static inline void parseOtaCheckJson(const String &body)
{
  clearOtaReleaseInfo();
  if (body.length() == 0)
    return;

  JsonDocument doc;
  if (deserializeJson(doc, body))
    return;

  if (strcmp(doc["result"] | "", "update") != 0)
    return;

  g_otaReleaseInfo.valid = true;
  strlcpy(g_otaReleaseInfo.version, doc["version"] | "", sizeof(g_otaReleaseInfo.version));

  const char *relLayout = doc["release_layout"] | doc["fs_ota_layout"] | "";
  strlcpy(g_otaReleaseInfo.release_layout, relLayout, sizeof(g_otaReleaseInfo.release_layout));

  {
    const char *clientLay = doc["client_fs_ota_layout"];
    if (!clientLay || !clientLay[0])
      clientLay = currentOtaFsLayoutId();
    if (!clientLay)
      clientLay = "";
    strlcpy(g_otaReleaseInfo.client_layout, clientLay,
            sizeof(g_otaReleaseInfo.client_layout));
  }

  strlcpy(g_otaReleaseInfo.release_name, doc["release_name"] | "", sizeof(g_otaReleaseInfo.release_name));
  strlcpy(g_otaReleaseInfo.match_kind, doc["match"] | "", sizeof(g_otaReleaseInfo.match_kind));
  strlcpy(g_otaReleaseInfo.binary_file, doc["binary_file"] | "", sizeof(g_otaReleaseInfo.binary_file));

  g_otaReleaseInfo.release_id = doc["release_id"] | 0;
  g_otaReleaseInfo.binary_bytes = doc["binary_bytes"] | 0;

  logOtaReleaseInfo("OTA resolve");
}

static inline void appendOtaReleaseInfo(JsonDocument &doc)
{
  if (!g_otaReleaseInfo.valid)
    return;
  doc["ota_release_version"] = g_otaReleaseInfo.version;
  doc["ota_release_layout"] = g_otaReleaseInfo.release_layout;
  doc["ota_client_layout"] = g_otaReleaseInfo.client_layout;
  doc["ota_release_name"] = g_otaReleaseInfo.release_name;
  doc["ota_match"] = g_otaReleaseInfo.match_kind;
  doc["ota_release_id"] = g_otaReleaseInfo.release_id;
  doc["ota_binary_bytes"] = g_otaReleaseInfo.binary_bytes;
  doc["ota_binary_file"] = g_otaReleaseInfo.binary_file;
}

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
inline int probeSketchUpdate()
{
  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, updateUrl))
    return -1;
  http.setTimeout(15000);
  http.setUserAgent(F("ESP8266-http-Update"));
  http.addHeader(F("x-ESP8266-mode"), F("sketch"));
  http.addHeader(F("x-ESP8266-version"), TRUC_VERSION);
  http.addHeader(F("x-ESP8266-action"), F("check"));
  addOtaLayoutHeaders(http);
  int code = http.GET();
  if (code == 200)
    parseOtaCheckJson(http.getString());
  else
    clearOtaReleaseInfo();
  http.end();
  return code;
}

static int _lastOtaLoggedPct = -1;

inline t_httpUpdate_return actualUpdate(bool sketch = false)
{
  WiFiClient client;
  ESPhttpUpdate.rebootOnUpdate(false);
  ESPhttpUpdate.setClientTimeout(120000);
  ESPhttpUpdate.closeConnectionsOnUpdate(true);

  _lastOtaLoggedPct = -1;
  ESPhttpUpdate.onProgress([](int current, int total) {
    if (total <= 0)
      return;
    int pct = (current * 100) / total;
    if (pct >= _lastOtaLoggedPct + 10 || pct >= 99)
    {
      _lastOtaLoggedPct = pct;
      Logf("OTA download %d%% (%d / %d bytes)\n", pct, current, total);
    }
  });

  Logf("OTA download start (local %s)\n", TRUC_VERSION);
  logOtaReleaseInfo("OTA selected");

  if (sketch) {
    HTTPClient http;
    if (!http.begin(client, updateUrl))
      return HTTP_UPDATE_FAILED;
    addOtaLayoutHeaders(http);
    t_httpUpdate_return ret = ESPhttpUpdate.update(http, TRUC_VERSION);
    ESPhttpUpdate.onProgress(nullptr);
    return ret;
  }
  t_httpUpdate_return ret = ESPhttpUpdate.updateFS(client, updateUrl, SPIFFS_VERSION);
  ESPhttpUpdate.onProgress(nullptr);
  return ret;
}