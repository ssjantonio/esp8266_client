#include "ConfigJson.h"
#include <EEPROM.h>

void convertToJson(const Config &src, JsonVariant dst) {

  dst["firmware_version"] = FIRMWARE_VERSION; //src.firmware_version;
  dst["wifi_mode"] = src.wifi_mode;
  dst["host_name"] = src.host_name;
  dst["identifier"] = src.identifier;
  dst["user_token"] = src.user_token;
  dst["admin_token"] = src.admin_token;
  dst["ssid"] = src.ssid;
  dst["password"] = src.password;
  dst["ip_type"] = src.ip_type;
  dst["ip_address"] = src.ip_address;
  dst["gateway"] = src.gateway;
  dst["subnet"] = src.subnet;
  dst["primary_dns"] = src.primary_dns;
  dst["secondary_dns"] = src.secondary_dns;
  dst["m_value"] = src.m_value;
  dst["b_value"] = src.b_value;
  dst["uart_time_out"] = src.uart_time_out;
  dst["avg_count"] = src.avg_count;
  dst["filter_alpha"] = src.filter_alpha;
  dst["umbral_error"] = src.umbral_error;
  dst["d_value"] = src.d_value;
  dst["t_value"] = src.t_value;
  dst["sensor_type"] = src.sensor_type;
  dst["server_port"] = src.server_port;
  dst["server_host"] = src.server_host;
  dst["tcp_client_enabled"] = src.tcp_client_enabled;
  dst["modbus_server_enabled"] = src.modbus_server_enabled;
  dst["modbus_port"] = src.modbus_port;
  dst["sensor_id"] = src.sensor_id;
  dst["group_id"] = src.group_id;
}

// (automatically called by `doc.set(config)`)
void convertFromJson(JsonVariantConst src, Config &dst) {
  
  dst.firmware_version = FIRMWARE_VERSION; //src["firmware_version"];
  dst.wifi_mode = src["wifi_mode"];
  dst.ip_type = src["ip_type"];
  dst.uart_time_out = src["uart_time_out"];
  strlcpy(dst.host_name, src["host_name"] | "", sizeof(dst.host_name));
  strlcpy(dst.identifier, src["identifier"] | "", sizeof(dst.identifier));
  strlcpy(dst.user_token, src["user_token"] | "", sizeof(dst.user_token));
  strlcpy(dst.admin_token, src["admin_token"] | "", sizeof(dst.admin_token));
  strlcpy(dst.ssid, src["ssid"] | "", sizeof(dst.ssid));
  strlcpy(dst.password, src["password"] | "", sizeof(dst.password));
  strlcpy(dst.ip_address, src["ip_address"] | "", sizeof(dst.ip_address));
  strlcpy(dst.gateway, src["gateway"] | "", sizeof(dst.gateway));
  strlcpy(dst.subnet, src["subnet"] | "", sizeof(dst.subnet));
  strlcpy(dst.primary_dns, src["primary_dns"] | "", sizeof(dst.primary_dns));
  strlcpy(dst.secondary_dns, src["secondary_dns"] | "", sizeof(dst.secondary_dns));
  strlcpy(dst.server_host, src["server_host"] | "", sizeof(dst.server_host));
  dst.server_port = src["server_port"];
  dst.tcp_client_enabled = src["tcp_client_enabled"] | 1;
  dst.modbus_server_enabled = src["modbus_server_enabled"] | 1;
  dst.modbus_port = src["modbus_port"] | 502;
  dst.m_value = src["m_value"];
  dst.b_value = src["b_value"];
  dst.avg_count = src["avg_count"];
  dst.filter_alpha =src["filter_alpha"];
  dst.umbral_error = src["umbral_error"];
  dst.d_value = src["d_value"];
  dst.t_value = src["t_value"];
  dst.sensor_type = src["sensor_type"];
  dst.sensor_id = src["sensor_id"];
  dst.group_id = src["group_id"];
}


uint8_t getRestartCount()
{
  uint8_t count = 0;
  EEPROM.get(0, count); 
  return count;
}

void setRestartCount(uint8_t count)
{
  EEPROM.put(0, count);
   EEPROM.commit();
}