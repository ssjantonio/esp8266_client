#pragma once 

#include <ArduinoJson.h>

#include "Config.h"

void convertToJson(const Config &src, JsonVariant dst);
void convertFromJson(JsonVariantConst src, Config &dst);
void setRestartCount(uint8_t count);
uint8_t getRestartCount();