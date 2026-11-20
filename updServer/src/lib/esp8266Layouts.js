/**
 * Layouts comunes del IDE Arduino ESP8266 (Flash size + FS + OTA dual).
 * id: convención {flash}M {fs} {ota} — fs/ota en K (KB) o M (MB) según escala.
 */

const ESP8266_LAYOUTS = [
  {
    id: '1M64K512K',
    flashBytes: 1048576,
    fsBytes: 65536,
    otaMin: 370000,
    otaMax: 560000,
    maxBinBytes: 524288,
    label: '1 MB (FS: 64 KB | OTA: ~512 KB)',
    hint: 'ESP8285, Tools → 1 MB (64 KB FS)',
  },
  {
    id: '1M128K384K',
    flashBytes: 1048576,
    fsBytes: 131072,
    otaMin: 350000,
    otaMax: 400000,
    maxBinBytes: 393216,
    label: '1 MB (FS: 128 KB | OTA: ~384 KB)',
    hint: 'Tools → 1 MB (128 KB SPIFFS)',
  },
  {
    id: '1M0K512K',
    flashBytes: 1048576,
    fsBytes: 0,
    otaMin: 480000,
    otaMax: 560000,
    maxBinBytes: 524288,
    label: '1 MB (sin FS | OTA: ~512 KB)',
    hint: 'Tools → 1 MB (no FS)',
  },
  {
    id: '2M512K512K',
    flashBytes: 2097152,
    fsBytes: 524288,
    otaMin: 480000,
    otaMax: 530000,
    maxBinBytes: 524288,
    label: '2 MB (FS: 512 KB | OTA: ~512 KB)',
    hint: 'Tools → 2 MB (512 KB SPIFFS)',
  },
  {
    id: '2M1M512K',
    flashBytes: 2097152,
    fsBytes: 1048576,
    otaMin: 480000,
    otaMax: 530000,
    maxBinBytes: 524288,
    label: '2 MB (FS: 1 MB | OTA: ~512 KB)',
    hint: 'Tools → 2 MB (1 MB SPIFFS)',
  },
  {
    id: '4M1M1M',
    flashBytes: 4194304,
    fsBytes: 1048576,
    otaMin: 950000,
    otaMax: 1100000,
    maxBinBytes: 1048576,
    label: '4 MB (FS: 1 MB | OTA: ~1 MB)',
    hint: 'Tools → 4 MB (1 MB FS)',
  },
  {
    id: '4M2M1M',
    flashBytes: 4194304,
    fsBytes: 2097152,
    otaMin: 950000,
    otaMax: 1100000,
    maxBinBytes: 1048576,
    label: '4 MB (FS: 2 MB | OTA: ~1 MB)',
    hint: 'Tools → 4 MB (2 MB FS)',
  },
  {
    id: '4M3M512K',
    flashBytes: 4194304,
    fsBytes: 3145728,
    otaMin: 480000,
    otaMax: 530000,
    maxBinBytes: 524288,
    label: '4 MB (FS: 3 MB | OTA: ~512 KB)',
    hint: 'Tools → 4 MB (3 MB FS)',
  },
  {
    id: 'ANY',
    flashBytes: 0,
    fsBytes: 0,
    maxBinBytes: 4 * 1024 * 1024,
    label: 'Universal (cualquier layout — usar solo si es seguro)',
    hint: 'Mismo .bin válido en todos los esquemas',
  },
];

const BY_ID = new Map(ESP8266_LAYOUTS.map((L) => [L.id, L]));

function getLayout(id) {
  if (!id) return null;
  return BY_ID.get(String(id)) || null;
}

function listUploadLayouts() {
  return ESP8266_LAYOUTS.filter((L) => L.id !== 'ANY');
}

function layoutLabel(id) {
  const L = getLayout(id);
  if (!L) return id ? String(id) : '—';
  return L.label;
}

function flashMbFromBytes(bytes) {
  if (!bytes || bytes < 512 * 1024) return null;
  return Math.round(bytes / (1024 * 1024));
}

/**
 * Deduce layout a partir de telemetría OTA (cabeceras del httpUpdate).
 */
function inferLayout({ flashBytes, fsBytes, otaBytes }) {
  if (!flashBytes) return null;

  let best = null;
  let bestScore = Infinity;

  for (const L of ESP8266_LAYOUTS) {
    if (L.id === 'ANY' || !L.flashBytes) continue;
    if (Math.abs(L.flashBytes - flashBytes) > 8192) continue;

    let score = 0;

    if (fsBytes > 0) {
      const dfs = Math.abs((L.fsBytes || 0) - fsBytes);
      if (dfs > 8192) continue;
      score += dfs;
    }

    if (otaBytes > 0 && L.otaMin != null && L.otaMax != null) {
      if (otaBytes < L.otaMin || otaBytes > L.otaMax) {
        if (fsBytes > 0) continue;
        const mid = (L.otaMin + L.otaMax) / 2;
        score += Math.abs(otaBytes - mid) / 4;
      } else {
        const mid = (L.otaMin + L.otaMax) / 2;
        score += Math.abs(otaBytes - mid) / 16;
      }
    }

    if (score < bestScore) {
      bestScore = score;
      best = L;
    }
  }

  if (best) return best.id;

  if (fsBytes > 0) {
    let fsMatch = null;
    let fsMatchCount = 0;
    for (const L of ESP8266_LAYOUTS) {
      if (L.id === 'ANY' || !L.flashBytes) continue;
      if (Math.abs(L.flashBytes - flashBytes) > 8192) continue;
      if (L.fsBytes !== fsBytes) continue;
      fsMatch = L;
      fsMatchCount++;
    }
    if (fsMatchCount === 1) return fsMatch.id;
  }

  return null;
}

function resolveClientLayout({ layoutHeader, flashBytes, fsBytes, otaBytes }) {
  const explicit = layoutHeader && getLayout(layoutHeader);
  if (explicit && explicit.id !== 'ANY') {
    return explicit.id;
  }
  return inferLayout({ flashBytes, fsBytes, otaBytes });
}

module.exports = {
  ESP8266_LAYOUTS,
  getLayout,
  listUploadLayouts,
  layoutLabel,
  flashMbFromBytes,
  inferLayout,
  resolveClientLayout,
};