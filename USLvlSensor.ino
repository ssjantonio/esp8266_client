#define ASYNC_WEBSERVER

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#ifndef ASYNC_WEBSERVER
#include <ESP8266WebServer.h>
#endif
#include <ESP8266mDNS.h>
#include <LittleFS.h>
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
const bool formatLittleFsOnFailure = true; //Formatear automaticamente el sistema de archivos en caso de falla
const char *config_filename = "/config.json"; //archivo de configuración

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

#ifndef ASYNC_WEBSERVER
void handleRoot()
#else
void handleRoot(AsyncWebServerRequest *request)
#endif
{
  Logln(F("Request home\n"));
  int sec = millis() / 1000;
  int min = sec / 60;
  int hr = min / 60;
  String sensor_type = "UNKNOWN";

  if(config.sensor_type == 0)
    sensor_type = F("US LEVEL A02YYUW");
  else if(config.sensor_type == 1)
    sensor_type = F("I-QY WEIGHT INDICATOR");
  else if(config.sensor_type == 2)
    sensor_type = F("DIBATEC LUMINA");

#ifdef ASYNC_WEBSERVER
  if (wantsJson(request))
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
    String jsonString;
    if (serializeJson(doc, jsonString) == 0)
      request->send(500, "application/json", F("{\"result\":\"serialize_failed\"}"));
    else
      request->send(200, "application/json", jsonString);
    return;
  }
#endif

  StreamString temp;
  if(!temp.reserve(1800)){
    Logln("No memory");
    #ifndef ASYNC_WEBSERVER
    server.send(200, "text/html", "NO MEMORY!");
    #else
    request->send(200, "text/html", "NO MEMORY!");
    #endif
    return;
  }  // Preallocate a large chunk to avoid memory fragmentation
  
  temp.printf("\
<html>\
  <head>\
    <meta http-equiv='refresh' content='5'/>\
    <title>Hidraquim SA de CV - IOT</title>\
    <style>\
      body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
    </style>\
  </head>\
  <body>\
    <h1>Modulo transmisor TCP interfaz UART TTL - AsyncWebserver</h1>\
    <p><strong>VERSION:</strong> %d</p>\
    <p><strong>HOST NAME:</strong> %s</p>\
    <p><strong>IDENTIFIER:</strong> %s</p>\
    <p><strong>SENSOR TYPE:</strong> %s</p>\
    <p><strong>CLIENT IP:</strong> %s</p>\
    <p><strong>SERVER HOST:</strong> %s:%d</p>\
    <p><strong>WIFI STRENGTH:</strong> %d</p>\
    <p><strong>PRESENT VALUE:</strong> %.2f</p>\
    <p><strong>UART ERROR:</strong> %d</p>\
    <p><strong>Uptime:</strong> %02d:%02d:%02d</p>\
    <form method=\"post\" action=\"/set-ssid\">\
    <p><label>SSID: </label><input name=\"ssid\" value=\"%s\"></input></p>\
    <p><label>SSID: </label><input name=\"pwd\" value=\"\"></input></p>\
    <p><button>Save</button></p></form>\
  </body>\
</html>", config.firmware_version, config.host_name, config.identifier, sensor_type.c_str(), _wifiIp,config.server_host, config.server_port, WiFi.RSSI(), present_value, distancia_error, hr, min % 60, sec % 60, config.ssid);
#ifndef ASYNC_WEBSERVER
  server.send(200, "text/html", temp.c_str());
#else  
  request->send(200, "text/html", temp.c_str());
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
  bool success = serializeJsonPretty(doc, jsonString) > 0;
  if (!success) {
    #ifndef ASYNC_WEBSERVER
    server.send(500, "application/json", F("{ \"result\"=\"Failed to serialize configuration\"}"));
    #else
    request->send(500, "application/json", F("{ \"result\"=\"Failed to serialize configuration\"}"));
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
    server.send(500, "application/json", F("{ \"result\"=\"Failed to deserialize configuration\"}"));
    Logln(err.f_str());
  }else{
    config = doc.as<Config>();
    config.firmware_version = FIRMWARE_VERSION; //este no se puede modificar!
    server.send(200, "application/json", "{ \"result\"=\"Ok\"}");
    saveConfigFile(config_filename, config);

    server.stop();
    delay(100);
    ESP.reset();
  }
  #else

  config = json.as<Config>();
  config.firmware_version = FIRMWARE_VERSION; //este no se puede modificar!
  request->send(200, "application/json", "{ \"result\"=\"Ok\"}");
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

    if(_reconnectTries > 5) //reiniciar despues de 5 intentos fallidos de conexión
      ESP.restart();
    
    _reconnecting = false;
    
  }
}

void connectWifi()
{
  Logln(F("\nConnecting to wifi..."));
  if(config.wifi_mode == WFSTA && _restartCount<3)
  {
      WiFi.mode(WIFI_STA);
      
      if(config.ip_type == IP_STATIC)
      {
        //Establecer manualmente la IP
        Logln(F("Setting Static IP."));

        local_ip.fromString(config.ip_address);
        gateway.fromString(config.gateway);
        subnet.fromString(config.subnet);
        primary_dns.fromString(config.primary_dns);
        secondary_dns.fromString(config.secondary_dns);

        WiFi.config(local_ip, gateway, subnet, primary_dns, secondary_dns); 
      }

      WiFi.begin((const char*)config.ssid, (const char*)config.password);

      int halfSecondsTried = 0;
      //Esperar la conexión
      while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        digitalWrite(LED_BUILTIN, LOW);
        delay(250);
        digitalWrite(LED_BUILTIN, HIGH);
        delay(250);
        halfSecondsTried++;
        if(halfSecondsTried/2 > 30)
        {
          Logln(F("Can't connect to the SSID, you can reset 3 times to connect as AP and configure in http://192.168.4.1/config"));
          digitalWrite(LED_BUILTIN, HIGH); //DEJAR EL LED ENCENDIDO
          break;
        }
      }

      WiFi.hostname(config.host_name);
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
      }else{
        last_error_code = 1000;
      }
      
      Logf("Connected to %s\n", config.identifier);
      Logf("IP address: %s\n", WiFi.localIP().toString().c_str());
      //_wifiIp = WiFi.localIP().toString().c_str();
      memcpy(_wifiIp, WiFi.localIP().toString().c_str(), 15);
      setRestartCount(0);
  }

  else if(config.wifi_mode == WFAP || getRestartCount()>=3 )
  {
      WiFi.softAP(config.identifier);

      IPAddress myIP = WiFi.softAPIP();
      Logf("AP IP address: %s\n", myIP.toString().c_str());
      Logf("Default AP: %s\n", config.identifier);

      setRestartCount(0);
  }

  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
}

ulong _restartAt = 0;
bool _needsRestart = false;

void setup(void) {
  
  pinMode(LED_BUILTIN, OUTPUT);
  //digitalWrite(led, 0);

  setupESPSerial();

  /*CONTEO DE REINICIOS, AL TERCER REINICIO SE CONECTA COMO AP*/
  EEPROM.begin(EEPROM_SIZE);
  _restartCount = getRestartCount();
  if(_restartCount > 3)
  {
    _restartCount = 0;
    setRestartCount(0); 
  }else 
    setRestartCount(_restartCount+1); //Aumentar la cuenta de reinicios, se resetea al conectarse al WIFI

  if (!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)) {
    last_error_code = 10300;
    Logln(F("Failed to mount LittleFS. Please format the partition."));
  }

  bool loaded = loadConfigFile(config_filename, config);
  if(!loaded) //Guardar parametros por default
    resetConfig();

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
    config = json.as<Config>();
    config.firmware_version = FIRMWARE_VERSION; //este no se puede modificar!
    request->send(200, "application/json", "{ \"result\"=\"Ok\"}");
    saveConfigFile(config_filename, config);
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

  Logln(F("Request set-config\n"));
  StreamString temp;
  if(!temp.reserve(1800)){
    Logln("No memory");
    request->send(200, "text/html", "NO MEMORY!");
    return;
  }

  temp.printf("\
<html>\
  <head>\
    <title>Hidraquim SA de CV - IOT</title>\
    <style>\
      body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
    </style>\
  </head>\
  <body>\
    <h1>Configución básica</h1>\
    <p><strong>Identificador</strong></p>\
    <form method=\"post\" action=\"/set-config\">\
    <p><label>HOST NAME:</label> <input name=\"host\" value=\"%s\"/></p>\
    <p><label>IDENTIFICADOR:</label> <input name=\"ident\" value=\"%s\"/></p>\
    <p><strong>Conexión WiFi</strong></p>\
    <p><label>Modo: </label><select name=\"mod\" value=\"%d\"><option value=\"0\">Access point</option><option value=\"1\">Estacion</option></select>\
    <p><label>SSID: </label><input name=\"ssid\" value=\"%s\"/></p>\
    <p><label>PASS: </label><input name=\"pwd\" value=\"\"/></p>\
    <p></p>\
    <p><strong>Servidor TCP</strong></p>\
    <p><label>Host: </label><input name=\"server\" value=\"%s\"/></p>\
    <p><label>Puerto: </label><input name=\"port\" value=\"%d\"/></p>\
    <p></p>\
    <p><strong>Tipo de sensor</strong></p>\
    <p><select name=\"st\" value=\"%d\">\
    <option value=\"0\">US A02YYUW</option>\
    <option value=\"1\">Rhino I-QY</option>\
    <option value=\"2\">Dibatec Lumina SS</option>\
    </select>\
    <p><label>Umbral: </label><input name=\"umbral\" value=\"%.2f\">\
    <p><button>Guardar</button></p></form>\
  </body>\
</html>",  config.host_name, config.identifier,config.wifi_mode, config.ssid, config.server_host, config.server_port, config.sensor_type, config.umbral_error);

  request->send(200, "text/html; charset=utf-8", temp.c_str());

  });

  server.on("/set-config", HTTP_POST, [](AsyncWebServerRequest* request) {
    String message;

    if (request->hasParam("host", true) && request->hasParam("ident", true)) {
      memcpy(config.host_name, request->getParam("host", true)->value().c_str(), 16);
      memcpy(config.identifier, request->getParam("ident", true)->value().c_str(), 16);  
      message = "Host and Identifier Set\n";
    } 

    if (request->hasParam("mod", true) && request->hasParam("ssid", true) && request->hasParam("pwd", true)) {
      String wifiMode = request->getParam("mod", true)->value();
      memcpy(config.ssid, request->getParam("ssid", true)->value().c_str(), 32);
      memcpy(config.password, request->getParam("pwd", true)->value().c_str(), 24);  
      if(wifiMode == "0") 
        config.wifi_mode = 0;
      else if(wifiMode == "1") 
        config.wifi_mode = 1;
      message += "SSID and Password Set\n";
    } 

    if(request->hasParam("st",true)){
      String st = request->getParam("st", true)->value();
      if(st  == "0") 
        config.sensor_type = 0;
      if(st == "1") 
        config.sensor_type = 1;
      if(st == "2") 
        config.sensor_type = 2;

        message += "Sensor type Set\n";
    }

    if (request->hasParam("server", true) && request->hasParam("port", true)) {
      
      memcpy(config.server_host, request->getParam("server", true)->value().c_str(), 32);
      config.server_port = request->getParam("port", true)->value().toInt();
      message += "Server Set\n";
    }
    
    saveConfigFile(config_filename, config);
    
    request->send(200, "text/plain", "Set-SSID Result: " + message);
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
    server.send(200, "application/json", "{ \"result\"=\"Ok\"}");
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
        server.send(200, "application/json", "{ \"result\"=\"Ok\"}");
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
        server.send(500, "application/json", F("{ \"result\"=\"Failed to load config\"}"));
      }else 
        server.send(200, "application/json", "{ \"result\"=\"Ok\"}");
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
    for(int i = 0; i<=17; i++)
      array.add(bufferDib[i]);

    for(int i = 0; i<=7; i++)
      arrayReal.add(real_part[i]);

    for(int i = 0; i<=3; i++)
      arrayDec.add(decimal_part[i]);
    
    String jsonString;
    bool success = serializeJsonPretty(doc, jsonString) > 0;
    if (!success) {
      request->send(500, "application/json", F("{ \"result\"=\"Failed to serialize weight\"}"));
    }
    else
      request->send(200, "application/json", jsonString);
    
  });

  server.on("/ver", [](AsyncWebServerRequest *request)
  {
    if (wantsJson(request))
    {
      JsonDocument doc;
      doc["version"] = TRUC_VERSION;
      doc["firmware_version"] = FIRMWARE_VERSION;
      doc["spiffs_version"] = SPIFFS_VERSION;
      String jsonString;
      serializeJson(doc, jsonString);
      request->send(200, "application/json", jsonString);
    }
    else
      request->send(200, "text/plain", TRUC_VERSION);
  });

  server.on("/log", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (wantsJson(request))
    {
      request->send(200, "application/json", serialLogJson());
      return;
    }

    String body;
    body.reserve(1400);
    body += F("<!DOCTYPE html><html><head><meta charset=\"utf-8\">");
    body += F("<meta http-equiv=\"refresh\" content=\"3\">");
    body += F("<title>Serial log</title>");
    body += F("<style>body{font-family:monospace;background:#111;color:#0f0;padding:12px}");
    body += F("pre{white-space:pre-wrap}</style></head><body>");
    body += F("<h3>Últimas líneas</h3><pre>");
    body += serialLogText();
    body += F("</pre><p><a href=\"/log\">refresh</a> · <a href=\"/log?format=json\">json</a></p></body></html>");
    request->send(200, "text/html; charset=utf-8", body);
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
    bool success = serializeJsonPretty(doc, jsonString) > 0;
    if (!success) {
      #ifndef ASYNC_WEBSERVER
      server.send(500, "application/json", F("{ \"result\"=\"Failed to serialize distance\"}"));
      #else
      request->send(500, "application/json", F("{ \"result\"=\"Failed to serialize distance\"}"));
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
  if (modbusSlave.isRunning())
  {
    modbusSlave.end();
    _modbusPausedForUpdate = true;
  }
  if (_tcpClient.connected())
    _tcpClient.close(true);
  _isConnecting = false;
  server.end();
  for (int i = 0; i < 20; i++)
  {
    delay(10);
    yield();
  }
  Logf("Heap antes de OTA: %u\n", ESP.getFreeHeap());
}

void resumeNetAfterUpdate()
{
  server.begin();
  _lastTCPAlive = millis();
  if (_modbusPausedForUpdate)
  {
    _modbusPausedForUpdate = false;
    startModbus();
  }
}

int last_level_miliseconds = 0;
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
  //Sumar la parte decimal
  for (int j = 0; j <= decimal_pos; j++)
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
            isNeg = false;
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
          if(in_real) //Parte entera
          {
              real_part[real_pos] = dataChar -48;
              real_pos++;
          }
          else if(in_decimal) //Parte decimal
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
  
  if(config.wifi_mode == WFSTA)
    MDNS.update();
    
  if(_restartCount < 3)
    checkWifiStatus();

  if(_restartCount < 3 && config.tcp_client_enabled)
    tcpConnect();

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
    Logf("OTA %s -> %s\n", TRUC_VERSION, updateUrl);
    pauseNetForUpdate();
    _last_update_result = actualUpdate(true);
    if (_last_update_result == HTTP_UPDATE_OK)
    {
      delay(100);
      ESP.reset();
    }
    resumeNetAfterUpdate();
  }

  // Sin tráfico TCP durante 5 minutos: reset de último recurso.
  // No corre si acabamos de consultar OTA (resumeNetAfterUpdate refresca el contador).
  if(config.tcp_client_enabled && config.server_port > 0 && (millis() - _lastTCPAlive) > 300000 && WiFi.status() == WL_CONNECTED )
  { 
    Logf("Restart ESP %d - %d", millis(), _lastTCPAlive);
    ESP.reset();
  }
}

