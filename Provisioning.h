#pragma once

#include <stddef.h>

#ifndef AP_PROVISIONING_PASSWORD
/* WPA2 requiere 8..63 caracteres; misma clave para WiFi AP y POST /ap-mode */
#define AP_PROVISIONING_PASSWORD "16794300"
#endif

bool provisioningPasswordOk(const char *given);
void provisioningApPsk(char *out, size_t outLen);