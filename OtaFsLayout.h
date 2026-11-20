#pragma once
/**
 * Catálogo fs_ota_layout — mantener alineado con updServer/src/lib/esp8266Layouts.js
 */
#include <Arduino.h>
#include <flash_hal.h>
#include <stdint.h>

struct OtaFsLayoutRow {
  const char *id;
  uint32_t flashBytes;
  uint32_t fsBytes;
  uint32_t otaMin;
  uint32_t otaMax;
};

static const OtaFsLayoutRow kOtaFsLayouts[] = {
    {"1M64K512K", 1048576, 65536, 370000, 560000},
    {"1M128K384K", 1048576, 131072, 350000, 400000},
    {"1M0K512K", 1048576, 0, 480000, 560000},
    {"2M512K512K", 2097152, 524288, 480000, 530000},
    {"2M1M512K", 2097152, 1024000, 480000, 530000},
    {"4M1M1M", 4194304, 1024000, 900000, 2800000},
    {"4M2M1M", 4194304, 2072576, 950000, 1100000},
    {"4M3M512K", 4194304, 3145728, 480000, 530000},
};

static inline const char *inferOtaFsLayout(uint32_t flashBytes, uint32_t fsBytes,
                                           uint32_t otaBytes)
{
  if (!flashBytes)
    return nullptr;

  const OtaFsLayoutRow *best = nullptr;
  uint32_t bestScore = UINT32_MAX;
  const size_t n = sizeof(kOtaFsLayouts) / sizeof(kOtaFsLayouts[0]);

  for (size_t i = 0; i < n; i++) {
    const OtaFsLayoutRow &L = kOtaFsLayouts[i];
    if (flashBytes > L.flashBytes) {
      if (flashBytes - L.flashBytes > 8192)
        continue;
    } else if (L.flashBytes - flashBytes > 8192) {
      continue;
    }

    uint32_t score = 0;

    if (fsBytes > 0) {
      uint32_t dfs = (L.fsBytes > fsBytes) ? (L.fsBytes - fsBytes) : (fsBytes - L.fsBytes);
      if (dfs > 8192)
        continue;
      score += dfs;
    }

    if (otaBytes > 0) {
      if (otaBytes < L.otaMin || otaBytes > L.otaMax) {
        if (fsBytes > 0)
          continue;
        uint32_t mid = (L.otaMin + L.otaMax) / 2;
        uint32_t d = (otaBytes > mid) ? (otaBytes - mid) : (mid - otaBytes);
        score += d / 4;
      } else {
        uint32_t mid = (L.otaMin + L.otaMax) / 2;
        uint32_t d = (otaBytes > mid) ? (otaBytes - mid) : (mid - otaBytes);
        score += d / 16;
      }
    }

    if (score < bestScore) {
      bestScore = score;
      best = &L;
    }
  }

  if (best)
    return best->id;

  // OTA libre varía con el sketch; flash+FS identifican el layout del IDE
  if (fsBytes > 0) {
    const OtaFsLayoutRow *fsMatch = nullptr;
    uint8_t fsMatchCount = 0;
    for (size_t i = 0; i < n; i++) {
      const OtaFsLayoutRow &L = kOtaFsLayouts[i];
      if (flashBytes > L.flashBytes) {
        if (flashBytes - L.flashBytes > 8192)
          continue;
      } else if (L.flashBytes - flashBytes > 8192) {
        continue;
      }
      if (L.fsBytes != fsBytes)
        continue;
      fsMatch = &L;
      fsMatchCount++;
    }
    if (fsMatchCount == 1)
      return fsMatch->id;
  }

  return nullptr;
}

/** Telemetría estándar OTA (mismos valores que las cabeceras HTTP). */
static inline const char *currentOtaFsLayoutId()
{
  return inferOtaFsLayout(ESP.getFlashChipRealSize(), FS_PHYS_SIZE,
                          ESP.getFreeSketchSpace());
}