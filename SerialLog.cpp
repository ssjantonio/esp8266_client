#include "SerialLog.h"
#include <stdarg.h>

static char s_lines[SERIAL_LOG_LINES][SERIAL_LOG_LINE_LEN];
static uint8_t s_count = 0;
static uint8_t s_next = 0;

void serialLogClear()
{
  s_count = 0;
  s_next = 0;
  memset(s_lines, 0, sizeof(s_lines));
}

static void pushCompleteLine(const char *line)
{
  if (!line)
    return;
  strncpy(s_lines[s_next], line, SERIAL_LOG_LINE_LEN - 1);
  s_lines[s_next][SERIAL_LOG_LINE_LEN - 1] = 0;
  s_next = (s_next + 1) % SERIAL_LOG_LINES;
  if (s_count < SERIAL_LOG_LINES)
    s_count++;
}

void serialLogLine(const char *line)
{
  if (!line || !line[0])
    return;

  // Partir por saltos de línea si vienen varios.
  char tmp[SERIAL_LOG_LINE_LEN];
  size_t n = 0;
  for (const char *p = line; *p; ++p)
  {
    if (*p == '\r')
      continue;
    if (*p == '\n')
    {
      tmp[n] = 0;
      if (n > 0)
        pushCompleteLine(tmp);
      n = 0;
      continue;
    }
    if (n < SERIAL_LOG_LINE_LEN - 1)
      tmp[n++] = *p;
  }
  if (n > 0)
  {
    tmp[n] = 0;
    pushCompleteLine(tmp);
  }
}

void Logf(const char *fmt, ...)
{
  char buf[SERIAL_LOG_LINE_LEN * 2];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  Serial.print(buf);
  serialLogLine(buf);
}

void Logln(const __FlashStringHelper *line)
{
  Serial.println(line);
  char buf[SERIAL_LOG_LINE_LEN];
  strncpy_P(buf, (PGM_P)line, SERIAL_LOG_LINE_LEN - 1);
  buf[SERIAL_LOG_LINE_LEN - 1] = 0;
  serialLogLine(buf);
}

void Logln(const char *line)
{
  Serial.println(line);
  serialLogLine(line);
}

void Logln(const String &line)
{
  Serial.println(line);
  serialLogLine(line.c_str());
}

void Logln(int value)
{
  Serial.println(value);
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", value);
  serialLogLine(buf);
}

void Logln(const IPAddress &ip)
{
  Serial.println(ip);
  serialLogLine(ip.toString().c_str());
}

String serialLogText()
{
  String out;
  out.reserve(SERIAL_LOG_LINES * SERIAL_LOG_LINE_LEN);
  if (s_count == 0)
  {
    out = "(sin mensajes aún)\n";
    return out;
  }

  uint8_t start = (s_count < SERIAL_LOG_LINES) ? 0 : s_next;
  for (uint8_t i = 0; i < s_count; i++)
  {
    uint8_t idx = (start + i) % SERIAL_LOG_LINES;
    out += s_lines[idx];
    out += '\n';
  }
  return out;
}

static void appendJsonEscaped(String &out, const char *s)
{
  out += '"';
  if (!s)
  {
    out += '"';
    return;
  }
  for (; *s; ++s)
  {
    char c = *s;
    if (c == '"' || c == '\\')
    {
      out += '\\';
      out += c;
    }
    else if (c == '\n')
      out += F("\\n");
    else if (c == '\r')
      out += F("\\r");
    else if (c == '\t')
      out += F("\\t");
    else if ((uint8_t)c < 0x20)
      continue;
    else
      out += c;
  }
  out += '"';
}

String serialLogJson()
{
  String out;
  out.reserve(SERIAL_LOG_LINES * SERIAL_LOG_LINE_LEN + 64);
  out += F("{\"count\":");
  out += String(s_count);
  out += F(",\"lines\":[");

  if (s_count > 0)
  {
    uint8_t start = (s_count < SERIAL_LOG_LINES) ? 0 : s_next;
    for (uint8_t i = 0; i < s_count; i++)
    {
      if (i > 0)
        out += ',';
      uint8_t idx = (start + i) % SERIAL_LOG_LINES;
      appendJsonEscaped(out, s_lines[idx]);
    }
  }

  out += F("]}");
  return out;
}
