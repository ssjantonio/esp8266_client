const fs = require('fs');
const path = require('path');

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

function loadReleases(dataDir) {
  ensureDataFile(dataDir);
  const raw = fs.readFileSync(releasesPath(dataDir), 'utf8');
  const data = JSON.parse(raw);
  if (!Array.isArray(data.releases)) {
    data.releases = [];
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
  if ([major, minor, patch].some((n) => Number.isNaN(n))) {
    return null;
  }
  return major * 100 + minor * 10 + patch;
}

function getLatestByType(dataDir, type) {
  const data = loadReleases(dataDir);
  const filtered = data.releases.filter((r) => r.type === type);
  if (filtered.length === 0) {
    return null;
  }
  filtered.sort((a, b) => b.numeric_version - a.numeric_version);
  return filtered[0];
}

function addRelease(dataDir, release) {
  const data = loadReleases(dataDir);
  const id =
    data.releases.reduce((max, r) => Math.max(max, r.id || 0), 0) + 1;
  const row = {
    id,
    name: release.name,
    user: release.user,
    released_at: new Date().toISOString(),
    numeric_version: release.numeric_version,
    version: release.version,
    type: release.type,
    binary_file: release.binary_file,
  };
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
  addRelease,
  listReleases,
};