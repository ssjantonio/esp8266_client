/** Cabeceras OTA de ESP8266httpUpdate y ESP32 HTTPUpdate (modo sketch). */

const {
  resolveClientLayout,
  flashMbFromBytes,
} = require('./esp8266Layouts');

const PLATFORMS = {
  esp8266: {
    userAgent: 'ESP8266-http-Update',
    headerPrefix: 'x-esp8266',
  },
  esp32: {
    userAgent: 'ESP32-http-Update',
    headerPrefix: 'x-esp32',
  },
};

function detectPlatform(req, expectedPlatform) {
  const ua = req.get('user-agent') || '';
  for (const [id, meta] of Object.entries(PLATFORMS)) {
    if (ua === meta.userAgent) {
      if (expectedPlatform && id !== expectedPlatform) {
        return { ok: false, reason: 'platform_mismatch', platform: id };
      }
      return { ok: true, platform: id, meta };
    }
  }
  return { ok: false, reason: 'bad_user_agent', userAgent: ua };
}

function readOtaRequest(req, meta) {
  const p = meta.headerPrefix;
  const chipRaw = parseInt(req.get(`${p}-chip-size`) || '0', 10);
  const chipSizeBytes = Number.isFinite(chipRaw) ? chipRaw : 0;
  const freeRaw = parseInt(req.get(`${p}-free-space`) || '0', 10);
  const freeOtaBytes = Number.isFinite(freeRaw) ? freeRaw : 0;
  const fsRaw = parseInt(req.get(`${p}-fs-partition-bytes`) || '0', 10);
  const fsPartitionBytes = Number.isFinite(fsRaw) ? fsRaw : 0;
  const layoutHeader = (req.get(`${p}-fs-ota-layout`) || '').trim();

  const flashMb = flashMbFromBytes(chipSizeBytes);
  const fsOtaLayout = resolveClientLayout({
    layoutHeader,
    flashBytes: chipSizeBytes,
    fsBytes: fsPartitionBytes,
    otaBytes: freeOtaBytes,
  });

  return {
    mode: req.get(`${p}-mode`) || '',
    version: req.get(`${p}-version`) || '',
    action: (req.get(`${p}-action`) || '').toLowerCase(),
    chipSizeBytes,
    flashMb,
    freeOtaBytes,
    fsPartitionBytes,
    layoutHeader,
    fsOtaLayout,
    sketchSize: parseInt(req.get(`${p}-sketch-size`) || '0', 10) || 0,
    sdk: req.get(`${p}-sdk-version`) || '',
  };
}

const SKETCH_MODES = new Set([
  'sketch',
  'spiffs',
  'littlefs',
  'fatfs',
  'flashfs',
]);

function isSketchMode(mode) {
  return mode === 'sketch';
}

module.exports = {
  PLATFORMS,
  detectPlatform,
  readOtaRequest,
  isSketchMode,
  SKETCH_MODES,
};