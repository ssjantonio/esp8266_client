const fs = require('fs');
const path = require('path');
const { getLayout, flashMbFromBytes } = require('./esp8266Layouts');

function releasesPath(dataDir) {
  return path.join(dataDir, 'releases.json');
}

function ensureDataFile(dataDir) {
  const file = releasesPath(dataDir);
  if (!fs.existsSync(dataDir)) {
    fs.mkdirSync(dataDir, { recursive: true });
  }
  if (!fs.existsSync(file)) {
    fs.writeFileSync(file, JSON.stringify({ releases: [] }, null, 2), 'utf8');
  }
}

function normalizeRelease(release) {
  if (!release.board) release.board = '';
  let layout = (release.fs_ota_layout || '').trim();
  if (layout === '0') layout = '';
  release.fs_ota_layout = layout;

  const layoutDef = layout ? getLayout(layout) : null;
  if (layoutDef && layoutDef.flashBytes) {
    release.flash_mb = flashMbFromBytes(layoutDef.flashBytes) || release.flash_mb || 0;
  } else if (
    release.flash_mb === undefined ||
    release.flash_mb === null ||
    release.flash_mb === ''
  ) {
    release.flash_mb = 0;
  } else {
    release.flash_mb = parseInt(release.flash_mb, 10) || 0;
  }

  const parsed = parseNumericVersion(release.version);
  if (parsed !== null) {
    release.numeric_version = parsed;
  }
  return release;
}

function loadReleases(dataDir) {
  ensureDataFile(dataDir);
  const raw = fs.readFileSync(releasesPath(dataDir), 'utf8');
  const data = JSON.parse(raw);
  if (!Array.isArray(data.releases)) {
    data.releases = [];
  }
  let dirty = false;
  for (const release of data.releases) {
    const before = JSON.stringify(release);
    normalizeRelease(release);
    if (JSON.stringify(release) !== before) dirty = true;
  }
  if (dirty) {
    saveReleases(dataDir, data);
  }
  return data;
}

function saveReleases(dataDir, data) {
  fs.writeFileSync(releasesPath(dataDir), JSON.stringify(data, null, 2), 'utf8');
}

function parseNumericVersion(version) {
  const parts = String(version).split('_');
  if (parts.length !== 3) {
    return null;
  }
  const major = parseInt(parts[0], 10);
  const minor = parseInt(parts[1], 10);
  const patch = parseInt(parts[2], 10);
  if ([major, minor, patch].some((n) => Number.isNaN(n) || n < 0 || n > 99)) {
    return null;
  }
  return major * 10000 + minor * 100 + patch;
}

function isUniversalRelease(release) {
  const layout = release.fs_ota_layout || '';
  if (layout === 'ANY') return true;
  if (layout) return false;
  return !(release.flash_mb || 0);
}

/**
 * Elige el .bin: misma plataforma, versión nueva, layout FS/OTA compatible.
 */
function getBestRelease(dataDir, { platform, clientVersionNum, flashMb, fsOtaLayout }) {
  const data = loadReleases(dataDir);
  let candidates = data.releases.filter((r) => r.type === platform);
  if (clientVersionNum !== null) {
    candidates = candidates.filter(
      (r) => r.numeric_version > clientVersionNum
    );
  }
  if (candidates.length === 0) return null;

  candidates.sort((a, b) => b.numeric_version - a.numeric_version);
  const targetVer = candidates[0].numeric_version;
  const atVersion = candidates.filter((r) => r.numeric_version === targetVer);

  if (fsOtaLayout) {
    const exact = atVersion.find((r) => r.fs_ota_layout === fsOtaLayout);
    if (exact) return exact;
  }

  // Transición: firmware viejo o sin bin por layout → release solo flash_mb (sin fs_ota_layout)
  if (flashMb) {
    const legacyFlash = atVersion.find(
      (r) => !r.fs_ota_layout && r.flash_mb === flashMb
    );
    if (legacyFlash) return legacyFlash;
  }

  const universal = atVersion.find((r) => isUniversalRelease(r));
  if (universal) return universal;

  if (fsOtaLayout) {
    return null;
  }

  const legacyUniversal = atVersion.find(
    (r) => !r.fs_ota_layout && !(r.flash_mb || 0)
  );
  return legacyUniversal || null;
}

function getLatestByType(dataDir, type) {
  return getBestRelease(dataDir, {
    platform: type,
    clientVersionNum: null,
    flashMb: null,
    fsOtaLayout: null,
  });
}

function addRelease(dataDir, release) {
  const data = loadReleases(dataDir);
  const id =
    data.releases.reduce((max, r) => Math.max(max, r.id || 0), 0) + 1;
  const row = normalizeRelease({
    id,
    name: release.name,
    user: release.user,
    released_at: new Date().toISOString(),
    numeric_version: release.numeric_version,
    version: release.version,
    type: release.type,
    fs_ota_layout: release.fs_ota_layout ?? '',
    flash_mb: release.flash_mb ?? 0,
    board: release.board ?? '',
    binary_file: release.binary_file,
  });
  data.releases.push(row);
  saveReleases(dataDir, data);
  return row;
}

function listReleases(dataDir) {
  const data = loadReleases(dataDir);
  return [...data.releases].sort(
    (a, b) => b.numeric_version - a.numeric_version
  );
}

module.exports = {
  ensureDataFile,
  loadReleases,
  saveReleases,
  parseNumericVersion,
  getLatestByType,
  getBestRelease,
  addRelease,
  listReleases,
};