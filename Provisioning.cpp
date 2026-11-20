#include "Provisioning.h"
#include "Config.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

bool provisioningPasswordOk(const char *given)
{
  if (!given)
    return false;
  if (strcmp(given, AP_PROVISIONING_PASSWORD) == 0)
    return true;
  /* Compat. clave corta anterior (6 dígitos) */
  return strcmp(given, "167943") == 0;
}

void provisioningApPsk(char *out, size_t outLen)
{
  const char *psk = AP_PROVISIONING_PASSWORD;
  const size_t n = strlen(psk);
  if (n >= 8)
    strlcpy(out, psk, outLen);
  else
    snprintf(out, outLen, "%s00", psk);
}