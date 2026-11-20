#include "FactoryIdentity.h"
#include <ESP8266WiFi.h>
#include <stdio.h>
#include <string.h>

#ifndef ID_PREFIX
#define ID_PREFIX "HQ_IOT_"
#endif

static bool hasAssignedPrefix(const char *id)
{
  const size_t n = strlen(ID_PREFIX);
  if (strncmp(id, ID_PREFIX, n) != 0)
    return false;
  return id[n] != '\0';
}

static bool needsFactoryIdentity(const Config &cfg)
{
  if (hasAssignedPrefix(cfg.identifier))
    return false;
  if (cfg.identifier[0] == '\0')
    return true;
#ifdef IDENTIFIER
  if (strcmp(cfg.identifier, IDENTIFIER) == 0)
    return true;
#endif
  return false;
}

bool ensureFactoryIdentity(Config &cfg)
{
  if (!needsFactoryIdentity(cfg))
    return false;

  const uint32_t chip = ESP.getChipId();
  snprintf(cfg.identifier, sizeof(cfg.identifier), "%s%06X", ID_PREFIX, chip & 0xFFFFFFU);

  if (cfg.host_name[0] == '\0')
    strlcpy(cfg.host_name, cfg.identifier, sizeof(cfg.host_name));

  return true;
}