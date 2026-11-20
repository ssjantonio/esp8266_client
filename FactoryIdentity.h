#pragma once

#include "Config.h"

// Asigna identifier HQ_IOT_<chipId> una sola vez; no sobrescribe IDs legados ni HQ_IOT_ ya guardados.
bool ensureFactoryIdentity(Config &cfg);