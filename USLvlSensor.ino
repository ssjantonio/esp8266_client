#define ASYNC_WEBSERVER

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#ifndef ASYNC_WEBSERVER
#include <ESP8266WebServer.h>
#endif
#include <ESP8266mDNS.h>
#include <LittleFS.h>
#include <flash_hal.h>
#include "OtaFsLayout.h"
#include <stdio.h>
#include <EEPROM.h>
#include <ESPAsyncTCP.h>
#ifdef ASYNC_WEBSERVER
#include <ESPAsyncWebServer.h>
#include "AsyncJson.h"
#include "ArduinoJson.h"
#include "AutoUpdate.h"
#endif

extern "C" {
#include <osapi.h>
#include <os_type.h>
}

#include "ConfigJson.h"
#include "SensorData.h"
#include "ModbusTcpSlave.h"
#include "SerialLog.h"
#include "FactoryIdentity.h"
#include "Provisioning.h"

//lowpassfilter
unsigned char bufferDib[18];
float real_part[7] = {0,0,0,0,0,0,0};
float decimal_part[3] = {0,0};
int bufferDBPos = 0;
float alpha = 0.05;
float filteredDistance = 0;
float avgDistance = 0;
float prevDistance = 0;
int currentAvgCount = 0;
float sumDistance = 0;
float d_distance = 0;
float present_value = 0;
bool _isConnecting = false;
bool _wasConnected = false;
uint8_t _restartCount = 0;
uint8_t _reconnectTries = 0;
float distancia;            //distancia corregida con calibración lineal
int distancia_error = 0;    //Ultimo código de error
int last_error_code = 0;
bool _sendTCPData = false;
unsigned long _lastTCPAlive = 0;
bool _enable_update = false;
t_httpUpdate_return _last_update_result = HTTP_UPDATE_OK;

Config config;  //archivo de configuración
const char *config_filename = "/config.json"; //archivo de configuración
static bool g_littleFsOk = false;

static bool mountLittleFs()
{
  if (FS_PHYS_SIZE == 0) {
    g_littleFsOk = false;
    last_error_code = 10300;
    Serial.println(
      "LittleFS: particion FS=0 en este build. IDE: Flash size con FS "
      "(p. ej. 1MB FS:64KB), no FS:none.");
    Logln(F("Failed to mount LittleFS. Please format the partition."));
    return false;
  }

  LittleFSConfig cfg(true);
  LittleFS.setConfig(cfg);
  if (LittleFS.begin()) {
    g_littleFsOk = true;
    FSInfo info;
    if (LittleFS.info(info))
      Serial.printf("LittleFS OK: %u / %u bytes\n", info.usedBytes, info.totalBytes);
    return true;
  }

  Serial.println(F("LittleFS: montaje falló, formateando..."));
  if (LittleFS.format() && LittleFS.begin()) {
    g_littleFsOk = true;
    Serial.println(F("LittleFS OK tras format"));
    return true;
  }

  g_littleFsOk = false;
  last_error_code = 10300;
  Serial.printf(
    "LittleFS no disponible. Flash IDE=%u real=%u — revisa Tools>Flash size\n",
    ESP.getFlashChipSize(), ESP.getFlashChipRealSize());
  Logln(F("Failed to mount LittleFS. Please format the partition."));
  return false;
}

unsigned char data[4] = {};  //Datos del lector US
//unsigned char data16[16] = {};  //Datos del lector US
unsigned char avgData[4] = {};

unsigned long _lastSendTCPMS = 0;

#ifndef ASYNC_WEBSERVER
ESP8266WebServer server(80); //El servidor
#else
AsyncWebServer server(80);
#endif
const int led = 0;

typedef union
{
  float number;
  uint8_t bytes[4];
} FLOATUNION_t;

#include "config.h"

static os_timer_t intervalTimer;

TCP_Packet_t tcpInfo;
TCP_Device_t tcpDevice;
char _wifiIp[16] = "No IP Set";
static void sendTCPDeviceData(void* arg)
{
  //os_timer_disarm(&intervalTimer);
  AsyncClient* client = reinterpret_cast<AsyncClient*>(arg);

	// send reply
	if (client->space() > 60 && client->canSend()) {
    
    tcpDevice.sensor.padding = 8;
    tcpDevice.sensor.headStart = 0x32;
    tcpDevice.sensor.version = FIRMWARE_VERSION;
    tcpDevice.sensor.sensorId = config.sensor_id;
    tcpDevice.sensor.groupId = config.group_id;
    tcpDevice.sensor.sensorType = config.sensor_type;
    tcpDevice.sensor.command = 1;
    tcpDevice.sensor.length = sizeof(deviceData_t);
    memcpy(tcpDevice.sensor.host_name, config.host_name, sizeof(config.host_name));
    memcpy(tcpDevice.sensor.identifier, config.identifier, sizeof(config.identifier));
    memcpy(tcpDevice.sensor.wifi_ip,_wifiIp, sizeof(tcpDevice.sensor.wifi_ip));
    int size = sizeof(deviceData_t);
    uint16_t crc = calcTCPPacketCrc((const char*)tcpDevice.TCPPacket, size);

    tcpDevice.sensor.crc = crc;

		int bytesAdded = client->add((const char *)tcpDevice.TCPPacket, size);
    //Logf("\n Added: %d, CRC: %d/%d, Byte13: %d, Byte14: %d", bytesAdded, crc, tcpDevice.sensor.crc, tcpDevice.TCPPacket[size-2], tcpDevice.TCPPacket[size-1]);
		client->send();
	}else
  {
    if(!client->space())
      Logln(F("NO hay espacio disponible en el cliente (sendTCPDeviceData)"));
    if(!client->canSend())
      Logln(F("NO esta disponible el cliente (sendTCPDeviceData)"));
  }
}


static void sendTCPSensorData(void* arg) 
{
	AsyncClient* client = reinterpret_cast<AsyncClient*>(arg);

	// send reply
	if (client->space() > 32 && client->canSend()) {
    _sendTCPData = false;
    tcpInfo.sensor.padding = 8;
    tcpInfo.sensor.headStart = 0x32;
    tcpInfo.sensor.version = FIRMWARE_VERSION;
    tcpInfo.sensor.sensorId = config.sensor_id;
    tcpInfo.sensor.groupId = config.group_id;
    tcpInfo.sensor.sensorType = config.sensor_type;
    tcpInfo.sensor.value = present_value;
    tcpInfo.sensor.length = sizeof(sensorData_t);
    tcpInfo.sensor.command = 0;
    int size = sizeof(sensorData_t);
    uint16_t crc = calcTCPPacketCrc((const char*)tcpInfo.TCPPacket, size);
    
    tcpInfo.sensor.crc = crc;

		int bytesAdded = client->add((const char *)tcpInfo.TCPPacket, size);
    //Logf("\n Added: %d, CRC: %d/%d, Byte13: %d, Byte14: %d\n", bytesAdded, crc, tcpInfo.sensor.crc, tcpInfo.TCPPacket[size-2], tcpInfo.TCPPacket[size-1]);
		client->send();
	}else
  {
    if(!client->space())
      Logln(F("NO hay espacio disponible en el cliente (sendTCPSensorData)"));
    if(!client->canSend())
      Logln(F("NO esta disponible el cliente (sendTCPSensorData)"));
  }
}

/* event callbacks */
static void handleData(void* arg, AsyncClient* client, void *data, size_t len) 
{
  TCP_Request_t req;
  if(len == sizeof(req.TCPPacket))
  {
    memcpy(req.TCPPacket, data, sizeof(req.TCPPacket));

    if(req.data.command == 0)
    {
      //os_timer_arm(&intervalTimer, 1, true); // schedule for reply to server at next 1ms
      //sendTCPSensorData(client);
      _sendTCPData = true;
    }
    else if(req.data.command == 1)
    {
      sendTCPDeviceData(client);
    }
    //Logf("\n data received from %s, command: %d \n", client->remoteIP().toString().c_str(),req.data.command);
    //Serial.write((uint8_t*)data, len);

  }else
  {
    Logln("La longitud no es correcta");
  }
  _isConnecting = false;
  _lastTCPAlive = millis();
}

void onConnect(void* arg, AsyncClient* client) 
{
  _lastTCPAlive = millis();
  _wasConnected = true;
	Logf("\n client has been connected to %s on port %d \n", config.server_host, config.server_port);
  sendTCPDeviceData(client);
}

unsigned long previousMillisConnectTcp = 0;
unsigned long intervalConnectTcp = 2000;

void onDisconnect(void* arg, AsyncClient* client) 
{
	//Logf("\n client has been disconnected from %s on port %d \n", config.server_host, config.server_port);
  _isConnecting = false;
}

static void onTcpError(void* arg, AsyncClient* client, int8_t error)
{
  (void)arg;
  (void)client;
  (void)error;
  _isConnecting = false;
}

AsyncClient _tcpClient;
void tcpInitialize()
{
	AsyncClient* client = &_tcpClient;
	client->onData(&handleData, client);
	client->onConnect(&onConnect, client);
	client->onDisconnect(&onDisconnect, client);
	client->onError(&onTcpError, client);
	os_timer_disarm(&intervalTimer);
  _lastTCPAlive = millis();
	//os_timer_setfn(&intervalTimer, &sendTCPSensorData, client);
}



void tcpConnect()
{
  unsigned long currentMilis = millis();
  if (currentMilis - previousMillisConnectTcp >= intervalConnectTcp)
  {
    previousMillisConnectTcp = currentMilis;
    AsyncClient* client = &_tcpClient;
    if(config.tcp_client_enabled && config.server_port > 0 && WiFi.status() == WL_CONNECTED && !client->connected() && !client->connecting() && !_isConnecting)
    {
      //Logf("\n connect to tcp://%s:%d", config.server_host, config.server_port);
      _isConnecting = client->connect(config.server_host, config.server_port);
    }
  }
}

bool loadConfigFile(const char *filename, Config &config) {
  File file = LittleFS.open(filename, "r");
  if (!file) {
    Logln(F("Failed to open config file"));
    last_error_code = 10404;
    return false;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, file);

  if (err) {
    Logln(F("Failed to deserialize configuration: "));
    Logln(err.f_str());
    last_error_code = 10405;
    return false;
  }

  // Extract config from the JSON document
  config = doc.as<Config>();

  return true;
}

// Saves the configuration to a file
void saveConfigFile(const char *filename, const Config &config) {
  if (!g_littleFsOk) {
    Logln(F("Failed to create config file"));
    last_error_code = 10400;
    return;
  }
  // Open file for writing
  File file = LittleFS.open(filename, "w");
  if (!file) {
    Logln(F("Failed to create config file"));
    last_error_code = 10400;
    return;
  }

  JsonDocument doc;
  doc.set(config);

  // Serialize JSON to file
  bool success = serializeJsonPretty(doc, file) > 0;
  if (!success) {
    last_error_code = 10401;
    Logln(F("Failed to serialize configuration"));
  }
}

#ifdef ASYNC_WEBSERVER
static bool wantsJson(AsyncWebServerRequest *request)
{
  // Explícito por query: siempre JSON
  if (request->hasArg("format") && request->arg("format").equalsIgnoreCase("json"))
    return true;

  // Solo si Accept es exclusivamente application/json (opcional ;charset=... o q=...)
  // Si viene text/html u otros tipos (navegador), se responde como antes.
  if (!request->hasHeader("Accept"))
    return false;

  String accept = request->header("Accept");
  accept.trim();
  accept.toLowerCase();
  if (accept.length() == 0 || accept == F("*/*"))
    return false;

  // Rechazar si hay más de un tipo (coma) o si no empieza por application/json
  if (accept.indexOf(',') >= 0)
    return false;
  if (!accept.startsWith(F("application/json")))
    return false;

  // application/json o application/json;...
  if (accept.length() > 16 && accept.charAt(16) != ';')
    return false;

  return true;
}
#endif

static const char *sensorTypeName()
{
  if (config.sensor_type == 0) return "US LEVEL A02YYUW";
  if (config.sensor_type == 1) return "I-QY WEIGHT INDICATOR";
  if (config.sensor_type == 2) return "DIBATEC LUMINA";
  return "UNKNOWN";
}

static void appendHqCss(String &html)
{
  html += F(
    "<style>"
    "*,*::before,*::after{box-sizing:border-box}"
    "body{margin:0;font:15px/1.45 system-ui,-apple-system,sans-serif;background:#0f1419;color:#e7eef7}"
    ".w{max-width:480px;margin:20px auto;padding:0 12px 28px}"
    ".c{background:#1a2332;border-radius:14px;padding:20px 18px;box-shadow:0 10px 28px #0008}"
    "h1{font-size:1.25rem;margin:0 0 4px}p.s{margin:0 0 12px;color:#8b9bb4;font-size:.9rem}"
  ".h{font-size:.72rem;text-transform:uppercase;letter-spacing:.08em;color:#6b7a90;margin:20px 0 10px;"
  "padding-bottom:6px;border-bottom:1px solid #ffffff14}"
    ".r{display:flex;justify-content:space-between;gap:12px;padding:8px 0;border-bottom:1px solid #ffffff12}"
    ".k{color:#8b9bb4}.v{font-weight:600;text-align:right}"
    "label{display:block;margin:0 0 6px;color:#9aabc2;font-size:.84rem}"
    "input:not([type=checkbox]),select,button{display:block;width:100%;max-width:100%;margin:0 0 12px;"
    "padding:10px 12px;border-radius:8px;border:1px solid #3a4a5e;background:#121a24;color:#e7eef7;font:inherit}"
    "select{-webkit-appearance:none;appearance:none}"
    "select{background-image:url(\"data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='8'%3E%3Cpath fill='%239aabc2' d='M1 1l5 5 5-5'/%3E%3C/svg%3E\");"
    "background-repeat:no-repeat;background-position:right 12px center;padding-right:36px}"
    "input[type=number]{-moz-appearance:textfield}"
    ".tog{display:flex;align-items:center;gap:12px;margin:0 0 14px;padding:12px;border-radius:10px;"
    "background:#121a24;border:1px solid #3a4a5e}"
    ".tog input{width:20px;height:20px;margin:0;flex:0 0 auto;accent-color:#3d9cf0}"
    ".tog label{margin:0;flex:1;font-size:.95rem;color:#e7eef7}"
    "button{background:#3d9cf0;border:0;font-weight:600;margin:8px 0 0;cursor:pointer}"
    ".n{margin-top:18px;display:flex;gap:14px;flex-wrap:wrap}.n a{color:#3d9cf0;text-decoration:none}"
    ".ok{color:#3ecf8e}"
    "</style>");
}

static void appendToggle(String &html, const char *fieldId, const char *fieldName, const char *label, bool on)
{
  html += F("<div class=tog><input type=checkbox name=");
  html += fieldName;
  html += F(" value=1 id=");
  html += fieldId;
  if (on)
    html += F(" checked");
  html += F("><label for=");
  html += fieldId;
  html += F(">");
  html += label;
  html += F("</label></div>");
}

static void appendHqRow(String &html, const __FlashStringHelper *k, const String &v)
{
  html += F("<div class=r><span class=k>");
  html += k;
  html += F("</span><span class=v>");
  html += v;
  html += F("</span></div>");
}

#ifndef ASYNC_WEBSERVER
void handleRoot()
#else
void handleRoot(AsyncWebServerRequest *request)
#endif
{
  const char *sensor_type = sensorTypeName();
  unsigned long sec = millis() / 1000;

#ifdef ASYNC_WEBSERVER
  if (wantsJson(request))
#endif
  {
    JsonDocument doc;
    doc["firmware_version"] = config.firmware_version;
    doc["host_name"] = config.host_name;
    doc["identifier"] = config.identifier;
    doc["sensor_type"] = config.sensor_type;
    doc["sensor_type_name"] = sensor_type;
    doc["client_ip"] = _wifiIp;
    doc["server_host"] = config.server_host;
    doc["server_port"] = config.server_port;
    doc["wifi_rssi"] = WiFi.RSSI();
    doc["present_value"] = present_value;
    doc["uart_error"] = distancia_error;
    doc["uptime_sec"] = sec;
    doc["tcp_client_enabled"] = config.tcp_client_enabled;
    doc["modbus_server_enabled"] = config.modbus_server_enabled;
    doc["modbus_port"] = config.modbus_port;
    doc["ota_version"] = TRUC_VERSION;
    doc["free_heap"] = ESP.getFreeHeap();
    doc["free_sketch"] = ESP.getFreeSketchSpace();
    String jsonString;
    if (serializeJson(doc, jsonString) == 0)
    {
#ifndef ASYNC_WEBSERVER
      server.send(500, "application/json", F("{\"result\":\"serialize_failed\"}"));
#else
      request->send(500, "application/json", F("{\"result\":\"serialize_failed\"}"));
#endif
    }
    else
    {
#ifndef ASYNC_WEBSERVER
      server.send(200, "application/json", jsonString);
#else
      request->send(200, "application/json", jsonString);
#endif
    }
#ifdef ASYNC_WEBSERVER
    return;
#endif
  }

#ifdef ASYNC_WEBSERVER
  String html;
  html.reserve(1600);
  html += F("<!doctype html><html><head><meta charset=utf-8>");
  html += F("<meta name=viewport content=\"width=device-width,initial-scale=1\">");
  html += F("<meta http-equiv=refresh content=30><title>");
  html += config.identifier;
  html += F("</title>");
  appendHqCss(html);
  html += F("</head><body><div class=w><div class=c>");
  html += F("<h1>Hidraquim IoT</h1><p class=s>");
  html += config.host_name;
  html += F(" · ");
  html += TRUC_VERSION;
  html += F("</p>");

  appendHqRow(html, F("Identifier"), String(config.identifier));
  appendHqRow(html, F("Sensor"), String(sensor_type));
  appendHqRow(html, F("Value"), String(present_value, 2));
  appendHqRow(html, F("UART error"), String(distancia_error));
  appendHqRow(html, F("IP"), String(_wifiIp));
  appendHqRow(html, F("TCP server"), String(config.server_host) + ":" + String(config.server_port));
  appendHqRow(html, F("WiFi RSSI"), String(WiFi.RSSI()) + " dBm");
  appendHqRow(html, F("Modbus"),
    config.modbus_server_enabled ? ("on :" + String(config.modbus_port)) : String("off"));
  appendHqRow(html, F("Uptime"), String(sec / 3600) + "h " + String((sec / 60) % 60) + "m");
  appendHqRow(html, F("Free OTA"), String(ESP.getFreeSketchSpace() / 1024) + " KB");

  html += F("<div class=n><a href=/set-config>Config</a><a href=/map>Map</a>");
  html += F("<a href=/log?format=json>Log</a><a href=/?format=json>JSON</a></div>");
  html += F("</div></div></body></html>");
  request->send(200, "text/html; charset=utf-8", html);
#endif
}

#ifndef ASYNC_WEBSERVER
void handleNotFound() 
#else
void handleNotFound(AsyncWebServerRequest *request)
#endif
{
  String message = "File Not Found\n\n";
  message += "URI: ";
#ifndef ASYNC_WEBSERVER  
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) { message += " " + server.argName(i) + ": " + server.arg(i) + "\n"; }
  server.send(404, "text/plain", message);
#else
  message += request->url();
  message += "\nMethod: ";
  message += (request->method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += request->args();
  message += "\n";
  for (uint8_t i = 0; i < request->args(); i++) { message += " " + request->argName(i) + ": " + request->arg(i) + "\n"; }
  request->send(404, "text/plain", message);
#endif

}

void setupESPSerial() {
  if(USE_TTL)
    Serial.begin(BAUD_RATE, SERIAL_8N1);
  else
    Serial.begin(115200);
  // readBytes() por defecto espera 1 s y congela el loop si la trama viene corta.
  Serial.setTimeout(50);
}

#ifndef ASYNC_WEBSERVER
void sendConfig() 
#else
void sendConfig(AsyncWebServerRequest *request)
#endif
{
  JsonDocument doc;
  doc.set(config);
  String jsonString;
  bool success = serializeJson(doc, jsonString) > 0;
  if (!success) {
    #ifndef ASYNC_WEBSERVER
    server.send(500, "application/json", F("{\"result\":\"serialize_failed\"}"));
    #else
    request->send(500, "application/json", F("{\"result\":\"serialize_failed\"}"));
    #endif
  }
  else
#ifndef ASYNC_WEBSERVER
    server.send(200, "application/json", jsonString);
#else
    request->send(200, "application/json", jsonString);
#endif
}
#ifndef ASYNC_WEBSERVER
void receiveConfig() 
#else
void receiveConfig(AsyncWebServerRequest *request, JsonVariant &json)
#endif
{
  #ifndef ASYNC_WEBSERVER
  String jsonString = server.arg("plain");
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, jsonString);
  if (err) {
    server.send(500, "application/json", F("{\"result\":\"Failed to deserialize configuration\"}"));
    Logln(err.f_str());
  }else{
    config = doc.as<Config>();
    config.firmware_version = FIRMWARE_VERSION; //este no se puede modificar!
    server.send(200, "application/json", "{\"result\":\"Ok\"}");
    saveConfigFile(config_filename, config);

    server.stop();
    delay(100);
    ESP.reset();
  }
  #else

  config = json.as<Config>();
  config.firmware_version = FIRMWARE_VERSION; //este no se puede modificar!
  request->send(200, "application/json", "{\"result\":\"Ok\"}");
  saveConfigFile(config_filename, config);

  server.end();
  delay(100);
  ESP.reset();
  #endif


}

void resetConfig()
{
  Logln(F("Using default config"));
  config.firmware_version = FIRMWARE_VERSION;
  config.wifi_mode = WFAP;
  strcpy(config.host_name, HOSTNAME);
  strcpy(config.identifier, IDENTIFIER);
  strcpy(config.user_token, USERTOKEN);
  strcpy(config.admin_token, ADMINTOKEN);
  config.ip_type = IP_DYNAMIC;
  strcpy(config.ssid, STASSID);
  strcpy(config.password, STAPSK);
  strcpy(config.ip_address, "192.168.1.254");
  strcpy(config.gateway, "192.168.0.1");
  strcpy(config.subnet, "255.255.255.0");
  strcpy(config.primary_dns, "1.1.1.1");
  strcpy(config.secondary_dns, "8.8.8.8");
  strcpy(config.server_host,"192.168.1.217");
  config.server_port = 1000;
  config.tcp_client_enabled = 1;
  config.modbus_server_enabled = 1;
  config.modbus_port = 502;
  config.m_value = 1;
  config.b_value = 0;
  config.uart_time_out = 5;
  config.d_value = 0.1;
  config.t_value = 4;
  config.avg_count = 2;
  config.filter_alpha = 0.01;
  config.umbral_error = 0.1;
  config.sensor_type = 0;
  config.sensor_id = 1;
  config.group_id = 1;
  saveConfigFile(config_filename, config);
}

IPAddress local_ip;
IPAddress gateway;
IPAddress subnet;
IPAddress primary_dns;
IPAddress secondary_dns; 
unsigned long previousMillis = 0;
unsigned long interval = 30000;
bool _hasSuccessfulConnection = false;
unsigned long previousReconnectMillis = 0;
unsigned long intervalReconnect = 30000;
bool _reconnecting = false;

#ifndef WIFI_STA_CONNECT_ATTEMPTS
#define WIFI_STA_CONNECT_ATTEMPTS 5
#endif
#ifndef WIFI_STA_ATTEMPT_TIMEOUT_SEC
#define WIFI_STA_ATTEMPT_TIMEOUT_SEC 12
#endif
#ifndef WIFI_AP_RECOVERY_MS
#define WIFI_AP_RECOVERY_MS 60000UL
#endif

static bool _apRecoveryMode = false;
static bool _apClientAttached = false;
static unsigned long _apRecoveryStartMs = 0;

static void startSoftAccessPoint(bool recoveryWindow);
static void onStaLinkUp();
static bool attemptStaConnectOnce(int attemptIndex, int attemptTotal);
static void pollApRecoveryTimeout();

static void startSoftAccessPoint(bool recoveryWindow)
{
  WiFi.disconnect(true);
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  delay(100);
  WiFi.mode(WIFI_AP);

  char apPsk[16];
  provisioningApPsk(apPsk, sizeof(apPsk));
  const IPAddress apIp(192, 168, 4, 1);
  const IPAddress apMask(255, 255, 255, 0);
  WiFi.softAPConfig(apIp, apIp, apMask);
  const uint8_t apChannel = 1;
  const bool apOk = WiFi.softAP(config.identifier, apPsk, apChannel, false, 4);

  IPAddress myIP = WiFi.softAPIP();
  Logf("AP SSID: %s  PSK: %s  ch=%u softAP=%s\n",
    config.identifier, apPsk, apChannel, apOk ? "ok" : "fail");
  Logf("AP IP address: %s\n", myIP.toString().c_str());

  if (recoveryWindow) {
    _apRecoveryMode = true;
    _apClientAttached = false;
    _apRecoveryStartMs = millis();
    Logf("Recovery AP: %lu s para asociar un cliente; si no, reintento STA\n",
      (unsigned long)(WIFI_AP_RECOVERY_MS / 1000));
  } else {
    _apRecoveryMode = false;
    setRestartCount(0);
  }

  WiFi.setAutoReconnect(false);
}

static void onStaLinkUp()
{
  if (MDNS.begin(config.host_name)) {
    _hasSuccessfulConnection = true;
    Logln(F("MDNS responder started"));
    if (config.modbus_server_enabled) {
      uint16_t mbPort = config.modbus_port > 0 ? (uint16_t)config.modbus_port : 502;
      MDNS.addService("uslvl", "tcp", mbPort);
      MDNS.addServiceTxt("uslvl", "tcp", "id", config.identifier);
      MDNS.addServiceTxt("uslvl", "tcp", "family", "ESP");
      MDNS.addServiceTxt("uslvl", "tcp", "map", "2");
    }
  } else {
    last_error_code = 1000;
  }

  Logf("Connected to %s\n", config.identifier);
  Logf("IP address: %s\n", WiFi.localIP().toString().c_str());
  memset(_wifiIp, 0, sizeof(_wifiIp));
  strncpy(_wifiIp, WiFi.localIP().toString().c_str(), sizeof(_wifiIp) - 1);
  setRestartCount(0);
  setWifiCyclePhase(WIFI_CYCLE_NORMAL);
  _reconnectTries = 0;
}

static bool attemptStaConnectOnce(int attemptIndex, int attemptTotal)
{
  Logf("STA intento %d/%d\n", attemptIndex, attemptTotal);
  WiFi.mode(WIFI_STA);
  WiFi.hostname(config.host_name);

  if (config.ip_type == IP_STATIC) {
    Logln(F("Setting Static IP."));
    local_ip.fromString(config.ip_address);
    gateway.fromString(config.gateway);
    subnet.fromString(config.subnet);
    primary_dns.fromString(config.primary_dns);
    secondary_dns.fromString(config.secondary_dns);
    WiFi.config(local_ip, gateway, subnet, primary_dns, secondary_dns);
  }

  WiFi.begin((const char*)config.ssid, (const char*)config.password);

  const int maxHalfSeconds = WIFI_STA_ATTEMPT_TIMEOUT_SEC * 2;
  int halfSecondsTried = 0;
  while (WiFi.status() != WL_CONNECTED && halfSecondsTried < maxHalfSeconds) {
    Serial.print(".");
    digitalWrite(LED_BUILTIN, LOW);
    delay(250);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(250);
    halfSecondsTried++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED)
    return true;

  WiFi.disconnect();
  return false;
}

static void pollApRecoveryTimeout()
{
  if (!_apRecoveryMode)
    return;

  if (WiFi.softAPgetStationNum() > 0) {
    if (!_apClientAttached) {
      _apClientAttached = true;
      Logln(F("Recovery AP: cliente asociado; permanece en AP para configurar"));
    }
    return;
  }

  if (!_apClientAttached && (millis() - _apRecoveryStartMs >= WIFI_AP_RECOVERY_MS)) {
    Logln(F("Recovery AP: sin clientes; reinicio para reintentar STA"));
    setWifiCyclePhase(WIFI_CYCLE_NORMAL);
    delay(100);
    ESP.restart();
  }
}

void checkWifiStatus()
{
   //print the Wi-Fi status every 30 seconds
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >=interval){
    switch (WiFi.status()){
      case WL_NO_SSID_AVAIL:
        Logln(F("Configured SSID cannot be reached"));
        break;
      case WL_CONNECTED:
        Logln(F("Connection successfully established"));
        break;
      case WL_CONNECT_FAILED:
        Logln(F("Connection failed"));
        break;
    }
    Logf("Connection status: %d\n", WiFi.status());
    Logf("RRSI: %d\n", WiFi.RSSI());
    previousMillis = currentMillis;

    if(WiFi.status() != WL_CONNECTED && !_reconnecting)
      reconnectWifi();
  }
}

void reconnectWifi()
{
  unsigned long currentMillis = millis();

  if(_hasSuccessfulConnection && currentMillis - previousReconnectMillis >= intervalReconnect) 
  {
    _reconnecting = true;
    Logln("Reconnecting to wifi");
    connectWifi();
    previousReconnectMillis = millis();
    
    _reconnectTries++;

    if (_reconnectTries >= WIFI_STA_CONNECT_ATTEMPTS) {
      Logln(F("STA: perdida prolongada; ciclo AP recuperacion"));
      setWifiCyclePhase(WIFI_CYCLE_AP_RECOVERY);
      delay(100);
      ESP.restart();
    }
    
    _reconnecting = false;
    
  }
}

void connectWifi()
{
  Logln(F("\nConnecting to wifi..."));

  if (config.wifi_mode == WFAP) {
    startSoftAccessPoint(false);
    return;
  }

  if (getWifiCyclePhase() == WIFI_CYCLE_AP_RECOVERY) {
    startSoftAccessPoint(true);
    return;
  }

  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  for (int attempt = 1; attempt <= WIFI_STA_CONNECT_ATTEMPTS; attempt++) {
    if (attemptStaConnectOnce(attempt, WIFI_STA_CONNECT_ATTEMPTS)) {
      onStaLinkUp();
      digitalWrite(LED_BUILTIN, HIGH);
      return;
    }
  }

  digitalWrite(LED_BUILTIN, HIGH);
  Logln(F("STA: sin WiFi; siguiente arranque: AP de recuperacion"));
  setWifiCyclePhase(WIFI_CYCLE_AP_RECOVERY);
  delay(100);
  ESP.restart();
}

ulong _restartAt = 0;
bool _needsRestart = false;

void setup(void) {
  
  pinMode(LED_BUILTIN, OUTPUT);
  //digitalWrite(led, 0);

  setupESPSerial();
  delay(50);
  // Diagnóstico flash/OTA (ESP8285 1MB: OTA debe ser > sketch)
  Serial.printf("\n=== FLASH / OTA ===\n");
  Serial.printf("TRUC_VERSION=%s\n", TRUC_VERSION);
  Serial.printf("Flash real:     %u bytes (%.1f KB)\n", ESP.getFlashChipRealSize(), ESP.getFlashChipRealSize() / 1024.0f);
  Serial.printf("Flash IDE:      %u bytes (%.1f KB)\n", ESP.getFlashChipSize(), ESP.getFlashChipSize() / 1024.0f);
  Serial.printf("FS partition:   %u bytes (%.1f KB)\n", FS_PHYS_SIZE, FS_PHYS_SIZE / 1024.0f);
  Serial.printf("Sketch actual:  %u bytes (%.1f KB)\n", ESP.getSketchSize(), ESP.getSketchSize() / 1024.0f);
  Serial.printf("Free OTA space: %u bytes (%.1f KB)\n", ESP.getFreeSketchSpace(), ESP.getFreeSketchSpace() / 1024.0f);
  {
    const uint32_t flashReal = ESP.getFlashChipRealSize();
    const uint32_t fsPart = FS_PHYS_SIZE;
    const uint32_t freeOta = ESP.getFreeSketchSpace();
    const char *otaLayout = inferOtaFsLayout(flashReal, fsPart, freeOta);
    Serial.printf("OTA layout:     %s (fs_ota_layout para servidor)\n",
                  otaLayout ? otaLayout : "(sin coincidencia en catálogo)");
    if (!otaLayout)
      Serial.printf(
          "  layout debug: flash=%u fs=%u free_ota=%u (sube OtaFsLayout.h o "
          "revisa Flash size en IDE)\n",
          flashReal, fsPart, freeOta);
  }
  Serial.printf("Free heap:      %u bytes\n", ESP.getFreeHeap());
  Serial.printf("===================\n\n");

  EEPROM.begin(EEPROM_SIZE);
  _restartCount = getRestartCount();
  if(_restartCount > 3)
  {
    _restartCount = 0;
    setRestartCount(0); 
  }else 
    setRestartCount(_restartCount+1); //Aumentar la cuenta de reinicios, se resetea al conectarse al WIFI

  mountLittleFs();

  bool loaded = loadConfigFile(config_filename, config);
  if(!loaded) //Guardar parametros por default
    resetConfig();

  if (ensureFactoryIdentity(config)) {
    saveConfigFile(config_filename, config);
    Logf("Factory identifier: %s\n", config.identifier);
  }

  if(config.avg_count == 0 || config.avg_count > 10)
    config.avg_count = 4;
  if(config.filter_alpha > 0.99 || config.filter_alpha <0)
    config.filter_alpha = 0; //Desactivar el filtro lowpass

  Logf("\nRestart count: %d\n", _restartCount);
  connectWifi();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/config", HTTP_GET, sendConfig); //obtener la configuración
  #ifndef ASYNC_WEBSERVER
  server.on("/config", HTTP_POST, receiveConfig); //postear la nueva configuración
  #else
  AsyncCallbackJsonWebHandler* handler = new AsyncCallbackJsonWebHandler("/config", [](AsyncWebServerRequest *request, JsonVariant &json) 
  {
    char keepId[sizeof(config.identifier)];
    strlcpy(keepId, config.identifier, sizeof(keepId));
    config = json.as<Config>();
    strlcpy(config.identifier, keepId, sizeof(config.identifier));
    config.firmware_version = FIRMWARE_VERSION; //este no se puede modificar!
    request->send(200, "application/json", "{\"result\":\"Ok\"}");
    saveConfigFile(config_filename, config);
    setWifiCyclePhase(WIFI_CYCLE_NORMAL);
    //server.end();
    //delay(100);
    //ESP.reset();
    _restartAt = millis() + 3000; //Resetear en 3 segundos
  });
  handler->setMethod(HTTP_POST);
  server.addHandler(handler);
  #endif

server.on("/set-config", HTTP_GET, [](AsyncWebServerRequest* request) {
  if (wantsJson(request))
  {
    JsonDocument doc;
    doc["host_name"] = config.host_name;
    doc["identifier"] = config.identifier;
    doc["wifi_mode"] = config.wifi_mode;
    doc["ssid"] = config.ssid;
    doc["server_host"] = config.server_host;
    doc["server_port"] = config.server_port;
    doc["sensor_type"] = config.sensor_type;
    doc["umbral_error"] = config.umbral_error;
    doc["tcp_client_enabled"] = config.tcp_client_enabled;
    doc["modbus_server_enabled"] = config.modbus_server_enabled;
    doc["modbus_port"] = config.modbus_port;
    String jsonString;
    serializeJson(doc, jsonString);
    request->send(200, "application/json", jsonString);
    return;
  }

  String html;
  html.reserve(2800);
  html += F("<!doctype html><html><head><meta charset=utf-8>");
  html += F("<meta name=viewport content=\"width=device-width,initial-scale=1\">");
  html += F("<title>Config</title>");
  appendHqCss(html);
  html += F("</head><body><div class=w><div class=c>");
  html += F("<h1>Configuración</h1><p class=s>");
  html += config.identifier;
  html += F("</p><form method=post action=/set-config>");
  html += F("<input type=hidden name=cfgform value=1>");
  html += F("<p class=h>Identidad</p>");
  html += F("<label>Host name</label><input name=host maxlength=15 value=\"");
  html += config.host_name;
  html += F("\"><p class=h>Identificador de fábrica</p>");
  html += F("<div class=r><span class=k>Identifier</span><span class=v>");
  html += config.identifier;
  html += F("</span></div><p class=s>No editable (prefijo HQ_IOT_).</p><p class=h>WiFi</p>");
  html += F("<label>Modo</label><select name=mod>");
  html += (config.wifi_mode == 0) ? F("<option value=0 selected>Access point</option>") : F("<option value=0>Access point</option>");
  html += (config.wifi_mode == 1) ? F("<option value=1 selected>Station</option>") : F("<option value=1>Station</option>");
  html += F("</select><label>SSID</label><input name=ssid maxlength=31 value=\"");
  html += config.ssid;
  html += F("\"><label>Password</label><input name=pwd type=password maxlength=23 placeholder=\"(sin cambio)\">");
  html += F("<p class=h>Comunicación</p>");
  appendToggle(html, "tcpcl", "tcpcl", "Cliente TCP (relay 0x32)", config.tcp_client_enabled != 0);
  html += F("<label>Servidor TCP</label><input name=server maxlength=31 value=\"");
  html += config.server_host;
  html += F("\"><label>Puerto TCP</label><input name=port type=number min=0 max=65535 value=\"");
  html += String(config.server_port);
  html += F("\">");
  appendToggle(html, "mben", "mben", "Servidor Modbus TCP", config.modbus_server_enabled != 0);
  html += F("<label>Puerto Modbus</label><input name=mbport type=number min=1 max=65535 value=\"");
  html += String(config.modbus_port > 0 ? config.modbus_port : 502);
  html += F("\"><p class=h>Sensor</p><label>Tipo</label><select name=st>");
  html += (config.sensor_type == 0) ? F("<option value=0 selected>US A02YYUW</option>") : F("<option value=0>US A02YYUW</option>");
  html += (config.sensor_type == 1) ? F("<option value=1 selected>Rhino I-QY</option>") : F("<option value=1>Rhino I-QY</option>");
  html += (config.sensor_type == 2) ? F("<option value=2 selected>Dibatec Lumina</option>") : F("<option value=2>Dibatec Lumina</option>");
  html += F("</select><label>Umbral error</label><input name=umbral type=number step=0.01 min=0 value=\"");
  html += String(config.umbral_error, 2);
  html += F("\"><button type=submit>Guardar</button></form>");
  html += F("<div class=n><a href=/>Home</a><a href=/map>Map</a><a href=/set-config?format=json>JSON</a></div>");
  html += F("</div></div></body></html>");
  request->send(200, "text/html; charset=utf-8", html);
});

  server.on("/set-config", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (request->hasParam("host", true))
      strlcpy(config.host_name, request->getParam("host", true)->value().c_str(), sizeof(config.host_name));

    if (request->hasParam("mod", true) && request->hasParam("ssid", true)) {
      String wifiMode = request->getParam("mod", true)->value();
      strlcpy(config.ssid, request->getParam("ssid", true)->value().c_str(), sizeof(config.ssid));
      if (request->hasParam("pwd", true) && request->getParam("pwd", true)->value().length() > 0)
        strlcpy(config.password, request->getParam("pwd", true)->value().c_str(), sizeof(config.password));
      if (wifiMode == "0")
        config.wifi_mode = 0;
      else if (wifiMode == "1")
        config.wifi_mode = 1;
    }

    if (request->hasParam("st", true)) {
      int st = request->getParam("st", true)->value().toInt();
      if (st >= 0 && st <= 2)
        config.sensor_type = st;
    }

    if (request->hasParam("server", true) && request->hasParam("port", true)) {
      strlcpy(config.server_host, request->getParam("server", true)->value().c_str(), sizeof(config.server_host));
      config.server_port = request->getParam("port", true)->value().toInt();
    }

    if (request->hasParam("cfgform", true)) {
      short tcpOn = request->hasParam("tcpcl", true) ? 1 : 0;
      if (tcpOn != config.tcp_client_enabled) {
        config.tcp_client_enabled = tcpOn;
        if (!tcpOn) {
          _sendTCPData = false;
          _isConnecting = false;
          if (_tcpClient.connected())
            _tcpClient.close(true);
        } else {
          _lastTCPAlive = millis();
        }
      }

      short mbOn = request->hasParam("mben", true) ? 1 : 0;
      if (request->hasParam("mbport", true)) {
        int mbPort = request->getParam("mbport", true)->value().toInt();
        if (mbPort > 0 && mbPort <= 65535)
          config.modbus_port = (short)mbPort;
      }
      if (mbOn != config.modbus_server_enabled) {
        config.modbus_server_enabled = mbOn;
        if (!mbOn && modbusSlave.isRunning())
          modbusSlave.end();
        else if (mbOn && WiFi.status() == WL_CONNECTED)
          startModbus();
      }
    }

    if (request->hasParam("umbral", true))
      config.umbral_error = request->getParam("umbral", true)->value().toFloat();

    saveConfigFile(config_filename, config);
    if (config.wifi_mode == WFSTA)
      setWifiCyclePhase(WIFI_CYCLE_NORMAL);

    String html;
    html.reserve(600);
    html += F("<!doctype html><html><head><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\">");
    html += F("<title>OK</title>");
    appendHqCss(html);
    html += F("</head><body><div class=w><div class=c><h1 class=ok>Guardado</h1>");
    html += F("<p class=s>Reinicia el equipo si cambiaste WiFi.</p>");
    html += F("<div class=n><a href=/set-config>Volver</a><a href=/>Home</a></div></div></div></body></html>");
    request->send(200, "text/html; charset=utf-8", html);
  });
/*
  server.on("/last-error", [](){
    server.send(200, "text/plain", String(last_error_code));
  });
  
 //cambiar el SSID  
  server.on("/set-sta", [](){
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    ssid.toCharArray(config.ssid, ssid.length()+1);
    password.toCharArray(config.password, password.length()+1);
    saveConfigFile(config_filename, config);
    server.send(200, "application/json", "{\"result\":\"Ok\"}");
    server.stop();
    delay(100);
    ESP.reset();
  });
//reiniciar la configuración
  server.on("/reset-config", [](){

      if (!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)) {
        server.send(200, "text/plain",F("Failed to mount LittleFS. Please format the partition."));
      }else{
        resetConfig();
        server.send(200, "application/json", "{\"result\":\"Ok\"}");
        server.stop();
        delay(100);
        ESP.reset();
      }
    });

  //Cargar la configuración
  server.on("/load-config", [](){
      bool loaded = loadConfigFile(config_filename, config);
      if(!loaded) //Guardar parametros por default
      {
        server.send(500, "application/json", F("{\"result\":\"Failed to load config\"}"));
      }else 
        server.send(200, "application/json", "{\"result\":\"Ok\"}");
  });
*/
#ifndef ASYNC_WEBSERVER
  server.on("/weight", []() {
#else
  server.on("/weight", [](AsyncWebServerRequest *request) {
#endif    
    JsonDocument doc;

    doc["weight"] = present_value;
    doc["error"] = distancia_error;
    doc["bufferpos"] = bufferDBPos;
    JsonArray array = doc.createNestedArray("data");
    JsonArray arrayReal = doc.createNestedArray("real");
    JsonArray arrayDec = doc.createNestedArray("rec");

    // add some values
    for(int i = 0; i < 18; i++)
      array.add(bufferDib[i]);

    for(int i = 0; i < 7; i++)
      arrayReal.add(real_part[i]);

    for(int i = 0; i < 3; i++)
      arrayDec.add(decimal_part[i]);
    
    String jsonString;
    if (serializeJson(doc, jsonString) == 0)
      request->send(500, "application/json", F("{\"result\":\"serialize_failed\"}"));
    else
      request->send(200, "application/json", jsonString);
  });

  {
    AsyncCallbackJsonWebHandler *provHandler = new AsyncCallbackJsonWebHandler(
        "/ap-mode",
        [](AsyncWebServerRequest *request, JsonVariant &json) {
          const char *given = json["password"] | "";
          if (!provisioningPasswordOk(given)) {
            request->send(403, "application/json", F("{\"result\":\"forbidden\"}"));
            return;
          }

          config.wifi_mode = WFAP;
          saveConfigFile(config_filename, config);
          setRestartCount(0);
          setWifiCyclePhase(WIFI_CYCLE_NORMAL);

          char apPsk[16];
          provisioningApPsk(apPsk, sizeof(apPsk));

          JsonDocument doc;
          doc["result"] = "Ok";
          doc["wifi_mode"] = 0;
          doc["identifier"] = config.identifier;
          doc["ap_ssid"] = config.identifier;
          doc["ap_password"] = apPsk;
          doc["ap_ip"] = "192.168.4.1";
          doc["restart_in_ms"] = 3000;
          String jsonString;
          serializeJson(doc, jsonString);
          request->send(200, "application/json", jsonString);
          _restartAt = millis() + 3000;
        });
    provHandler->setMethod(HTTP_POST);
    server.addHandler(provHandler);
  }

  server.on("/ver", [](AsyncWebServerRequest *request)
  {
    JsonDocument doc;
    doc["family"] = "Esp";
    doc["identifier"] = config.identifier;
    doc["host_name"] = config.host_name;
    doc["version"] = TRUC_VERSION;
    doc["firmware_version"] = FIRMWARE_VERSION;
    doc["spiffs_version"] = SPIFFS_VERSION;
    doc["wifi_mode"] = config.wifi_mode;
    if (config.wifi_mode == WFAP) {
      char apPsk[16];
      provisioningApPsk(apPsk, sizeof(apPsk));
      doc["ap_password"] = apPsk;
    }
    doc["free_heap"] = ESP.getFreeHeap();
    doc["free_sketch"] = ESP.getFreeSketchSpace();
    doc["flash_real_bytes"] = ESP.getFlashChipRealSize();
    doc["fs_partition_bytes"] = FS_PHYS_SIZE;
    if (const char *lay = currentOtaFsLayoutId())
      doc["fs_ota_layout"] = lay;
    String jsonString;
    serializeJson(doc, jsonString);
    request->send(200, "application/json", jsonString);
  });

  // Diagnóstico de memoria/partición (no dispara OTA ni escribe flash)
  server.on("/space", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    JsonDocument doc;
    doc["truc_version"] = TRUC_VERSION;
    doc["flash_real_bytes"] = ESP.getFlashChipRealSize();
    doc["flash_ide_bytes"] = ESP.getFlashChipSize();
    doc["sketch_bytes"] = ESP.getSketchSize();
    doc["free_ota_bytes"] = ESP.getFreeSketchSpace();
    doc["fs_partition_bytes"] = FS_PHYS_SIZE;
    if (const char *lay = currentOtaFsLayoutId())
      doc["fs_ota_layout"] = lay;
    doc["littlefs_ok"] = g_littleFsOk;
    doc["free_heap_bytes"] = ESP.getFreeHeap();
    doc["flash_real_kb"] = ESP.getFlashChipRealSize() / 1024.0f;
    doc["flash_ide_kb"] = ESP.getFlashChipSize() / 1024.0f;
    doc["sketch_kb"] = ESP.getSketchSize() / 1024.0f;
    doc["free_ota_kb"] = ESP.getFreeSketchSpace() / 1024.0f;
    String jsonString;
    serializeJson(doc, jsonString);
    request->send(200, "application/json", jsonString);
  });

  // Mapa Modbus tipado para promote del poller HQ.ModbusRealtime
  server.on("/map", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    const char *typeName = "UNKNOWN";
    const char *valueUnit = "";
    if (config.sensor_type == 0)
    {
      typeName = "US LEVEL A02YYUW";
      valueUnit = "m";
    }
    else if (config.sensor_type == 1)
    {
      typeName = "I-QY WEIGHT INDICATOR";
      valueUnit = "kg";
    }
    else if (config.sensor_type == 2)
    {
      typeName = "DIBATEC LUMINA";
      valueUnit = "kg";
    }

    JsonDocument doc;
    doc["family"] = "Esp";
    doc["mapVersion"] = MODBUS_MAP_VERSION;
    doc["sensorType"] = config.sensor_type;
    doc["sensorTypeName"] = typeName;
    doc["unitId"] = 1;
    doc["pollIntervalMs"] = 300;
    doc["timeoutMs"] = 2000;

    JsonArray ranges = doc.createNestedArray("ranges");
    JsonObject range0 = ranges.createNestedObject();
    range0["area"] = "Holding";
    range0["startTag"] = "0";
    range0["endTag"] = "22";

    JsonArray tags = doc.createNestedArray("tags");

    #define HQ_MAP_TAG(_tag, _addr, _dt, _regs, _chars, _unit) \
      do { \
        JsonObject o = tags.createNestedObject(); \
        o["tag"] = (_tag); \
        o["area"] = "Holding"; \
        o["address"] = (_addr); \
        o["dataType"] = (_dt); \
        o["byteOrder"] = "ABCD"; \
        o["registerCount"] = (_regs); \
        o["charCount"] = (_chars); \
        o["scale"] = 1; \
        o["unit"] = (_unit); \
      } while (0)

    HQ_MAP_TAG("value", 0, "Float32", 2, 0, valueUnit);
    HQ_MAP_TAG("error", 2, "UInt16", 1, 0, "");
    HQ_MAP_TAG("firmware", 3, "UInt16", 1, 0, "");
    HQ_MAP_TAG("sensor_id", 4, "UInt16", 1, 0, "");
    HQ_MAP_TAG("signature", 5, "UInt16", 1, 0, "");
    HQ_MAP_TAG("map_version", 6, "UInt16", 1, 0, "");
    HQ_MAP_TAG("identifier", 7, "String", 8, 16, "");
    HQ_MAP_TAG("host_name", 15, "String", 8, 16, "");
    #undef HQ_MAP_TAG

    String jsonString;
    serializeJson(doc, jsonString);
    request->send(200, "application/json", jsonString);
  });

  server.on("/log", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    request->send(200, "application/json", serialLogJson());
  });

  server.on("/update", [](AsyncWebServerRequest *request)
  {
    if(_last_update_result != HTTP_UPDATE_OK)
    {
      JsonDocument doc;
      doc["result"] = "HTTP_UPDATE_FAILED";
      doc["error"] = ESPhttpUpdate.getLastError();
      doc["error_string"] = ESPhttpUpdate.getLastErrorString();
      _last_update_result = HTTP_UPDATE_OK;
      String jsonString;
      serializeJson(doc, jsonString);
      request->send(200, "application/json", jsonString);
    }
    else
    {
      _enable_update = true;
      request->send(200, "application/json", F("{\"result\":\"OK updating...\"}"));
    }
  });
#ifndef ASYNC_WEBSERVER
  server.on("/distance", []() {
#else
  server.on("/distance", [](AsyncWebServerRequest *request) {
#endif    
    JsonDocument doc;

    doc["distance"] = distancia;
    doc["avg_distance"] = avgDistance;
    doc["f_distance"] = filteredDistance;
    doc["d_distance"] = d_distance;
    doc["error"] = distancia_error;

    JsonArray array = doc.createNestedArray("data");

    // add some values
    array.add(data[0]);
    array.add(data[1]);
    array.add(data[2]);
    array.add(data[3]);

    String jsonString;
    if (serializeJson(doc, jsonString) == 0) {
      #ifndef ASYNC_WEBSERVER
      server.send(500, "application/json", F("{\"result\":\"serialize_failed\"}"));
      #else
      request->send(500, "application/json", F("{\"result\":\"serialize_failed\"}"));
      #endif
    }
    else
    #ifndef ASYNC_WEBSERVER
      server.send(200, "application/json", jsonString);
    #else
      request->send(200, "application/json", jsonString);
    #endif
  });

  config.firmware_version = FIRMWARE_VERSION;
  server.onNotFound(handleNotFound);
  server.begin();

  tcpInitialize();
  startModbus();
}

static bool _modbusPausedForUpdate = false;

void seedModbusMap()
{
  modbusSlave.setHolding(3, (uint16_t)FIRMWARE_VERSION);
  modbusSlave.setHolding(4, (uint16_t)config.sensor_id);
  modbusSlave.setHolding(5, MODBUS_MAP_SIGNATURE);
  modbusSlave.setHolding(6, MODBUS_MAP_VERSION);
  modbusSlave.setAsciiBlock(7, config.identifier, 16);
  modbusSlave.setAsciiBlock(15, config.host_name, 16);
}

void startModbus()
{
  if (!config.modbus_server_enabled || WiFi.status() != WL_CONNECTED || modbusSlave.isRunning())
    return;

  uint16_t mbPort = config.modbus_port > 0 ? (uint16_t)config.modbus_port : 502;
  modbusSlave.begin(mbPort);
  seedModbusMap();
  Logf("Modbus TCP slave on :%u (holding 0..%d)\n", mbPort, MODBUS_HOLDING_COUNT - 1);
}

void pauseNetForUpdate()
{
  // Solo pausar Modbus + cliente TCP. NO hacer server.end():
  // en ESP8266 AsyncWebServer end()+begin() a menudo deja el puerto 80
  // muerto (ERR_CONNECTION_REFUSED) mientras Modbus sigue vivo porque
  // recrea su AsyncServer.
  if (modbusSlave.isRunning())
  {
    modbusSlave.end();
    _modbusPausedForUpdate = true;
  }
  if (_tcpClient.connected())
    _tcpClient.close(true);
  _isConnecting = false;
  for (int i = 0; i < 20; i++)
  {
    delay(10);
    yield();
  }
  Logf("Heap antes de OTA: %u\n", ESP.getFreeHeap());
}

void resumeNetAfterUpdate()
{
  _lastTCPAlive = millis();
  if (_modbusPausedForUpdate)
  {
    _modbusPausedForUpdate = false;
    startModbus();
  }
}

unsigned long last_level_miliseconds = 0;
int headStart = 0;
float sumDelta = 0;

int currentDerCount = 0;


bool in_real = false;
bool in_decimal = false;
int real_pos = 0;
int decimal_pos =0;
//unsigned char data_index = 0;
double _power[8] = {1, 10, 100, 1000, 10000, 100000, 1000000 };



bool inBufferDB = false;

bool isNeg = false;

void consumeDibatec(void)
{
  if (USE_TTL && 0 < Serial.available() ) 
  {  
    char data = Serial.read();
    if(data == 87){ //Inicio de buffer 'W'
      bufferDBPos = 0;
      inBufferDB = true;
    }
    else if(data == 13)
    {
      if (bufferDBPos < 18)
        bufferDib[bufferDBPos] = data;
      inBufferDB = false;
      parseBuffer(bufferDBPos-1);
      memset(bufferDib,0,sizeof(bufferDib)); //Poner a ceros
      Serial.flush();
    }
    if(inBufferDB && bufferDBPos < 18)
    {
      bufferDib[bufferDBPos] = data;
      bufferDBPos++;
    }
  }else {
    if (millis() - last_level_miliseconds > (1000*config.uart_time_out)) {
      distancia_error = -2;  //No se ha recibido señal en 5s
    }
  }
}

float calcValue()
{
  float result = 0;
  //Sumar la parte entera
  for(int i = real_pos-1; i>=0; i--)
    result += real_part[i] * _power[(real_pos - i - 1)]; // pow(10, (real_pos - i - 1));
  // decimal_pos es la cantidad de dígitos ya guardados (0..3).
  for (int j = 0; j < decimal_pos && j < 3; j++)
    result += ((float)decimal_part[j]) / _power[j+1]; //pow( 10, j + 1);
  return result;
}

void parseBuffer(int len)
{
  if(len > 17)
    return;

  char um[2] = { 0, 0 };
  memset(real_part,0,sizeof(real_part)); //Poner a ceros
  memset(decimal_part,0,sizeof(decimal_part));
  isNeg = false;
  in_decimal = false;
  decimal_pos = 0;
  real_pos = 0;

  for(int i=0; i < 18; i++) //Los 2 ultimos bytes tienen la unidad de medida
  {
    if(bufferDib[i] == 45) //-
      isNeg = true;

    if(bufferDib[i] == 46) //El siguiente es el decimal
        in_decimal =true;
    
    if(bufferDib[i] >= 48 && bufferDib[i] <= 57)
    {
      if(in_decimal && decimal_pos < 3){
        decimal_part[decimal_pos] = bufferDib[i] - 48;
        decimal_pos++;
      }else if(real_pos<7){
        real_part[real_pos] = bufferDib[i] - 48;
        real_pos++;
      }
    }
    if(bufferDib[i] == 76)
      um[0] = 76;
    else if(bufferDib[i] == 98)
      um[1] = 98;
    else if(bufferDib[i] == 75)
      um[0] = 75;
    else if(bufferDib[i] == 103)
      um[1] = 103;

  }
  
  //Unidad de medida en los 2 ultimos bytes
  //um[0] = bufferDib[len-2];
  //um[1] = bufferDib[len-1];

  float result = calcValue();
  
  if(um[0] == 76) //Libras, convertir a kg
      present_value = result/2.20462;
  else if(um[0] == 75)
    present_value=result; //Guardar el valor presente
  else if(um[1] == 103)
    present_value=result/1000.0; //Guardar el valor presente

  if(isNeg) //Si es negativo
    present_value = -1*present_value;

  distancia_error = 0;
  last_level_miliseconds = millis();
  Logf("Peso: %.2f", present_value);
}


void consumeIQYData(void)
{
  
  //Si hay datos disponibles
  if (USE_TTL && 0 < Serial.available()) {
    char dataChar = Serial.read();
/*
    if(data_index == 15){
      memset(data16,0,sizeof(data16));
      data_index = 0;
    }
    data16[data_index] = dataChar;
    data_index++;*/
    if(dataChar == 13)
    {
        if(in_decimal)
        {
          if(real_pos > 0) //Ya se leyo el entero, parsear a un flotante
          {
            present_value=calcValue(); //Guardar el valor presente
            if (isNeg)
              present_value = -present_value;
            isNeg = false;
            distancia_error = 0;
            last_level_miliseconds = millis();
            Serial.flush();
          }
          memset(real_part,0,sizeof(real_part)); //Poner a ceros
          memset(decimal_part,0,sizeof(decimal_part));
        }
        real_pos = 0; //Iniciar a recibir datos de la parte entera
        in_real = true;
        in_decimal = false;
    }else if(dataChar == 46) //Punto decimal
    {
        decimal_pos = 0; //Iniciar a recibir datos de la parte decimal
        in_real = false;
        in_decimal = true;
    }
    else
    {
        if(dataChar >= 48 && dataChar <= 57) //Un número del 0 al 9
        {
          if(in_real && real_pos < 7) //Parte entera
          {
              real_part[real_pos] = dataChar -48;
              real_pos++;
          }
          else if(in_decimal && decimal_pos < 3) //Parte decimal
          {
              decimal_part[decimal_pos] = dataChar -48;
              decimal_pos++;
          }
        }else if(dataChar == '-')
          isNeg = true;
    }
  }else {
    if (millis() - last_level_miliseconds > (1000*config.uart_time_out)) {
      distancia_error = -2;  //No se ha recibido señal en 5s
    }
  }
}

void consumeUSLevel(void)
{
  //Si hay datos disponibles
  if (USE_TTL && 0 < Serial.available()) {
    int sum;
    for(int i=0;i<headStart;i++) //Avanzar N bytes para encontrar el inicio de la cabecera
      Serial.read();
    int res = Serial.readBytes(data, 4);

    //Espera a que se complete la transmisión de datos salientes
    Serial.flush();

    if (data[0] == 0xff) {
      //Comprobación de los valores de lectura
      sum = (data[0] + data[1] + data[2]) & 0x00FF;
      /*Verifica que el resultado de la operación anterior coincida con el valor del dato 3
        Si no coincide, entonces la edición es errónea
      */
      if (sum == data[3]) {
        headStart = 0;
        distancia_error = 0;
        last_level_miliseconds = millis();

        //Conversión de los valores medidos en cm
        int newDistance = (data[1] << 8) + data[2];
        
        //distancia = (data[1] << 8) + data[2];
        if (newDistance > 30){
          
          newDistance /=10;
          //Corregir según la calibración lineal
          //newDistance = newDistance*config.m_value + config.b_value; 

          //Iniciar la distancia derivada
          if(avgDistance == 0){
            filteredDistance = newDistance;
            avgDistance = newDistance;
            distancia = newDistance;
            d_distance = newDistance;
          }

          //Si la diferencia es mayor al umbral de error, actualizar
          if(abs(newDistance - distancia) > config.umbral_error)             
            distancia = newDistance;

          //Promediar X resultados
          currentAvgCount++;
          currentDerCount++;
          sumDistance += distancia;
          sumDelta += (d_distance - distancia); //Acumular el delta para la derivada en el tiempo

          if(currentAvgCount >= config.avg_count) //Consideramos el promedio como tiempo para la derivada
          {
            currentAvgCount = 0; //Reiniciar el tiempo de la derivada
            avgDistance = (float)( (int)( (sumDistance/(config.avg_count + 1))*10)/10.0);
            sumDistance = avgDistance; //Reiniciar la suma
          }

          if(currentDerCount >= config.t_value) //Consideramos el promedio como tiempo para la derivada
          {
            currentDerCount = 0;
            if(abs(sumDelta) > config.d_value) //Filtrar por un umbral de error acumulado
            {
              sumDelta = 0;
              d_distance = distancia;
            }
          }

          //Lowpass filter
          filteredDistance = distancia*(1-config.filter_alpha) + prevDistance*config.filter_alpha;
          filteredDistance =(float) ((int)( filteredDistance*10)/10.0);
          prevDistance = filteredDistance;
        }else{ //Menor al rango de detección
          distancia = 0;
        }
      } else //No coincide el CheckSum
        distancia_error = -1;
    }else{
      //Ajustar el inicio de cabecera
      headStart = 0;
      int i = 0;
      for(i=0;i<4; i++)
      {
          if(data[i] == 0xff){
            headStart = i;
            break;
          }
      } 
    }
  } else {
    if (millis() - last_level_miliseconds > (1000*config.uart_time_out)) {
      distancia_error = -2;  //No se ha recibido señal en 5s
    }
  }
  present_value = distancia;
  //present_value +=0.01;
}
static void updateModbusHoldings()
{
  if (!modbusSlave.isRunning())
    return;

  // Mismo float que el paquete TCP 0x32, partido en 2 holding big-endian.
  union {
    float f;
    uint32_t u;
  } enc;
  enc.f = present_value;
  modbusSlave.setHolding(0, (uint16_t)((enc.u >> 16) & 0xFFFF));
  modbusSlave.setHolding(1, (uint16_t)(enc.u & 0xFFFF));
  modbusSlave.setHolding(2, (uint16_t)(distancia_error != 0 ? distancia_error : last_error_code));
  modbusSlave.setHolding(3, (uint16_t)FIRMWARE_VERSION);
  modbusSlave.setHolding(4, (uint16_t)config.sensor_id);
}

unsigned long _last_updated = 0;
bool _didFirstOtaCheck = false;
void loop(void) 
{
  if (_restartAt != 0 && (long)(millis() - _restartAt) >= 0) {
    _restartAt = 0;
    ESP.reset();
  }

  if(config.sensor_type == 0)
    consumeUSLevel();
  else if(config.sensor_type == 1)
    consumeIQYData();
  else if(config.sensor_type == 2)
    consumeDibatec();

  updateModbusHoldings();

#ifndef ASYNC_WEBSERVER  
  server.handleClient();
#endif  
  
  pollApRecoveryTimeout();

  if (config.wifi_mode == WFSTA && !_apRecoveryMode)
    MDNS.update();

  if (config.wifi_mode == WFSTA && !_apRecoveryMode) {
    checkWifiStatus();
    if (config.tcp_client_enabled)
      tcpConnect();
  }

  unsigned long currentMillis = millis();
  if(config.tcp_client_enabled && _sendTCPData && ((currentMillis-25) > _lastSendTCPMS) && _tcpClient.canSend()) //Cada 25ms minimo
  {
    _lastSendTCPMS = millis();
    sendTCPSensorData(&_tcpClient);
  }

  // OTA antes del watchdog TCP. Si el servidor TCP no contesta, el equipo
  // reinicia a los 5 min y el chequeo de 20 min nunca se alcanzaba.
  const unsigned long otaFirstMs = 45000UL;
  const unsigned long otaEveryMs = 1200000UL;
  bool otaDue = false;
  if (WiFi.status() == WL_CONNECTED)
  {
    if (_enable_update)
      otaDue = true;
    else if (!_didFirstOtaCheck && millis() >= otaFirstMs)
      otaDue = true;
    else if (_didFirstOtaCheck && (millis() - _last_updated) > otaEveryMs)
      otaDue = true;
  }

  if (otaDue)
  {
    _enable_update = false;
    _didFirstOtaCheck = true;
    _last_updated = millis();
    Logf("OTA check %s -> %s\n", TRUC_VERSION, updateUrl);
    int probe = probeSketchUpdate();
    if (probe == 200)
    {
      Logf("OTA bin disponible, pausando Modbus\n");
      pauseNetForUpdate();
      _last_update_result = actualUpdate(true);
      if (_last_update_result == HTTP_UPDATE_OK)
      {
        delay(100);
        ESP.reset();
      }
      else
        Logf("OTA fallo %d\n", (int)_last_update_result);
      resumeNetAfterUpdate();
    }
    else
    {
      _last_update_result = (probe == 304) ? HTTP_UPDATE_NO_UPDATES : HTTP_UPDATE_FAILED;
      Logf("OTA sin bin (%d), Modbus sigue\n", probe);
    }
  }

  // Reset solo si TCP y Modbus llevan 5 min callados.
  // Modbus solo alarga el plazo después de atender una trama; si nunca
  // respondió, no impide el reinicio. El chequeo OTA refresca el contador TCP.
  unsigned long aliveMs = _lastTCPAlive;
  if (config.modbus_server_enabled && modbusSlave.hasServedRequest())
  {
    unsigned long mbMs = modbusSlave.lastRequestMs();
    if ((long)(mbMs - aliveMs) > 0)
      aliveMs = mbMs;
  }
  bool tcpArmed = config.tcp_client_enabled && config.server_port > 0;
  bool modbusArmed = config.modbus_server_enabled;
  if ((tcpArmed || modbusArmed) && WiFi.status() == WL_CONNECTED && (millis() - aliveMs) > 300000)
  {
    Logf("Restart ESP tcp=%lu modbus=%lu served=%d\n", _lastTCPAlive, modbusSlave.lastRequestMs(), modbusSlave.hasServedRequest() ? 1 : 0);
    ESP.reset();
  }
}

