#pragma once

#include <Arduino.h>

#define SERIAL_LOG_LINES 10
#define SERIAL_LOG_LINE_LEN 96

void serialLogClear();
void serialLogLine(const char *line);
void Logf(const char *fmt, ...);
void Logln(const __FlashStringHelper *line);
void Logln(const char *line);
void Logln(const String &line);
void Logln(int value);
void Logln(IPAddress ip);

/** Texto plano de las últimas N líneas (para /log HTML). */
String serialLogText();

/** JSON: {"count":N,"lines":["..."]} */
String serialLogJson();
