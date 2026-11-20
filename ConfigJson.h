#pragma once 

#include <ArduinoJson.h>

#include "Config.h"

void convertToJson(const Config &src, JsonVariant dst);
void convertFromJson(JsonVariantConst src, Config &dst);
void setRestartCount(uint8_t count);
uint8_t getRestartCount();

/** EEPROM[1]: fase del ciclo STA↔AP de recuperación (no cambia wifi_mode en LittleFS). */
#define WIFI_CYCLE_NORMAL 0
#define WIFI_CYCLE_AP_RECOVERY 1
uint8_t getWifiCyclePhase();
void setWifiCyclePhase(uint8_t phase);