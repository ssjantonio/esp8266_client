const express = require('express');
const path = require('path');
const fs = require('fs');
const multer = require('multer');
const {
  listReleases,
  addRelease,
  parseNumericVersion,
} = require('../lib/releases');
const {
  listUploadLayouts,
  getLayout,
  layoutLabel,
} = require('../lib/esp8266Layouts');

const VERSION_RE = /^\d{1,2}_\d{1,2}_\d{1,2}$/;
const PLATFORMS = ['esp8266', 'esp32'];
const FLASH_OPTS = [0, 1, 2, 4, 8, 16];

/** Subida sin fs_ota_layout; OTA empareja por flash_mb (firmware antiguo). */
const LEGACY_FLASH_UPLOAD = {
  LEGACY_1M: { flash_mb: 1, maxBinBytes: 1024 * 1024, label: 'Legacy 1 MB (solo flash_mb)' },
  LEGACY_2M: { flash_mb: 2, maxBinBytes: 2 * 1024 * 1024, label: 'Legacy 2 MB (solo flash_mb)' },
  LEGACY_4M: { flash_mb: 4, maxBinBytes: 4 * 1024 * 1024, label: 'Legacy 4 MB (solo flash_mb)' },
};

function createUploadRouter({ dataDir, storageDir, basicAuth }) {
  const router = express.Router();
  router.use(basicAuth);

  for (const p of PLATFORMS) {
    fs.mkdirSync(path.join(storageDir, 'bin', p), { recursive: true });
  }

  const storage = multer.diskStorage({
    destination: (req, _file, cb) => {
      const type = PLATFORMS.includes(req.body?.platform)
        ? req.body.platform
        : 'esp8266';
      cb(null, path.join(storageDir, 'bin', type));
    },
    filename: (_req, file, cb) => {
      const safe = file.originalname.replace(/[^a-zA-Z0-9._-]/g, '_');
      cb(null, `${Date.now()}_${safe}`);
    },
  });

  const upload = multer({
    storage,
    limits: { fileSize: 4 * 1024 * 1024 },
    fileFilter: (_req, file, cb) => {
      if (!file.originalname.toLowerCase().endsWith('.bin')) {
        return cb(new Error('Solo archivos .bin'));
      }
      cb(null, true);
    },
  });

  router.get('/', (_req, res) => {
    const releases = listReleases(dataDir);
    res.type('html').send(renderPage(releases));
  });

  router.post('/', upload.single('binary_file'), (req, res) => {
    const name = (req.body.name || '').trim();
    const major = versionPart(req.body.major);
    const minor = versionPart(req.body.minor);
    const patch = versionPart(req.body.patch);
    const version = major === null ? '' : `${major}_${minor}_${patch}`;
    const type = PLATFORMS.includes(req.body.platform)
      ? req.body.platform
      : 'esp8266';
    const board = (req.body.board || '').trim().slice(0, 64);

    let fs_ota_layout = '';
    let flash_mb = 0;
    let maxBinFromLegacy = null;
    if (type === 'esp8266') {
      const layoutKey = (req.body.fs_ota_layout || '').trim();
      const legacy = LEGACY_FLASH_UPLOAD[layoutKey];
      if (legacy) {
        fs_ota_layout = '';
        flash_mb = legacy.flash_mb;
        maxBinFromLegacy = legacy.maxBinBytes;
      } else {
        fs_ota_layout = layoutKey;
        const layoutDef = getLayout(fs_ota_layout);
        if (!layoutDef) {
          return res
            .status(400)
            .send(errorPage('Seleccione un layout FS/OTA válido para ESP8266.'));
        }
        flash_mb = layoutDef.flashBytes
          ? Math.round(layoutDef.flashBytes / (1024 * 1024))
          : 0;
      }
    } else {
      flash_mb = parseInt(req.body.flash_mb || '0', 10) || 0;
    }

    if (
      !name ||
      major === null ||
      minor === null ||
      patch === null ||
      !VERSION_RE.test(version)
    ) {
      return res
        .status(400)
        .send(errorPage('Nombre, Major, Minor y Patch (0 a 99) son obligatorios.'));
    }
    if (!req.file) {
      return res.status(400).send(errorPage('Falta el archivo .bin.'));
    }

    const layoutDef =
      type === 'esp8266' && fs_ota_layout ? getLayout(fs_ota_layout) : null;
    const maxBin =
      maxBinFromLegacy || layoutDef?.maxBinBytes || 4 * 1024 * 1024;
    if (req.file.size > maxBin) {
      return res.status(400).send(
        errorPage(
          `El .bin (${req.file.size} bytes) supera el máximo del layout (${maxBin} bytes).`
        )
      );
    }

    const numeric_version = parseNumericVersion(version);
    if (numeric_version === null) {
      return res.status(400).send(errorPage('Versión inválida.'));
    }

    const relativePath = path
      .join('storage', 'bin', type, req.file.filename)
      .replace(/\\/g, '/');
    const user = req.auth?.user || 'admin';

    addRelease(dataDir, {
      name,
      user,
      version,
      type,
      fs_ota_layout,
      flash_mb,
      board,
      numeric_version,
      binary_file: relativePath,
    });

    res.redirect('/upload-esp8266');
  });

  return router;
}

function versionPart(value) {
  const n = parseInt(String(value ?? '').trim(), 10);
  if (Number.isNaN(n) || n < 0 || n > 99) return null;
  return n;
}

function releaseLayoutCell(b) {
  if (b.fs_ota_layout) {
    return `<code>${escapeHtml(b.fs_ota_layout)}</code><br><small class="text-muted">${escapeHtml(layoutLabel(b.fs_ota_layout))}</small>`;
  }
  if (b.flash_mb) {
    return `<span class="text-warning">legacy</span><br><small class="text-muted">flash ${b.flash_mb} MB (sin layout)</small>`;
  }
  return `<span class="text-muted">universal</span>`;
}

function errorPage(message) {
  return `<!DOCTYPE html><html lang="es"><head><meta charset="utf-8"><title>Error</title>
<link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/css/bootstrap.min.css" rel="stylesheet"></head>
<body class="p-4"><div class="alert alert-danger">${escapeHtml(message)}</div>
<a class="btn btn-primary" href="/upload-esp8266">Volver</a></body></html>`;
}

function escapeHtml(s) {
  return String(s)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

function renderPage(releases) {
  const layoutOptions = listUploadLayouts()
    .map(
      (L) =>
        `<option value="${escapeHtml(L.id)}">${escapeHtml(L.label)} — ${escapeHtml(L.hint || '')}</option>`
    )
    .join('');
  const legacyOptions = Object.entries(LEGACY_FLASH_UPLOAD)
    .map(
      ([id, L]) =>
        `<option value="${escapeHtml(id)}">${escapeHtml(L.label)} — transición firmware antiguo</option>`
    )
    .join('');
  const layoutOptionsAny =
    legacyOptions +
    `\n<option value="" disabled>──────── layouts IDE ────────</option>\n` +
    layoutOptions +
    `\n<option value="ANY">${escapeHtml(getLayout('ANY').label)}</option>`;

  const flashOptions = FLASH_OPTS.map((mb) =>
    mb === 0
      ? '<option value="0">Cualquiera (sin filtro flash)</option>'
      : `<option value="${mb}">${mb} MB</option>`
  ).join('');

  const rows = releases
    .map(
      (b) => `<tr>
        <td>${b.id}</td>
        <td>${escapeHtml(b.type || 'esp8266')}</td>
        <td>${releaseLayoutCell(b)}</td>
        <td>${escapeHtml(b.board || '')}</td>
        <td>${escapeHtml(b.name)}</td>
        <td>${escapeHtml(b.version)}</td>
        <td>${escapeHtml(b.user)}</td>
        <td>${escapeHtml(b.released_at)}</td>
      </tr>`
    )
    .join('');

  return `<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Subir firmware</title>
  <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/css/bootstrap.min.css" rel="stylesheet">
</head>
<body class="bg-light">
  <div class="container py-4">
    <h1 class="h3 mb-4">Firmware ESP8266 / ESP32</h1>
    <p class="text-muted">OTA: <code>/update/esp8266</code> · <code>/update/esp32</code>. En 8266 el servidor elige el <code>.bin</code> por <strong>layout FS/OTA</strong> (<code>fs_ota_layout</code>), deducido de <code>chip-size</code>, <code>free-space</code> y <code>fs-partition-bytes</code>.</p>
    <div class="card mb-4">
      <div class="card-header">Nueva versión</div>
      <div class="card-body">
        <form method="post" enctype="multipart/form-data" action="/upload-esp8266" id="uploadForm">
          <div class="mb-3">
            <label class="form-label">Plataforma</label>
            <select class="form-select" name="platform" id="platform" required>
              <option value="esp8266">ESP8266 / ESP8285</option>
              <option value="esp32">ESP32</option>
            </select>
          </div>
          <div class="mb-3" id="layoutBlock">
            <label class="form-label">Layout flash / FS / OTA</label>
            <select class="form-select" name="fs_ota_layout" id="fs_ota_layout" required>
              <option value="">— Elija el mismo que en Arduino IDE —</option>
              ${layoutOptionsAny}
            </select>
            <div class="form-text">ID guardado en <code>releases.json</code> (p. ej. <code>1M64K512K</code>, <code>4M2M1M</code>).</div>
          </div>
          <div class="mb-3 d-none" id="flashBlock">
            <label class="form-label">Flash objetivo (ESP32)</label>
            <select class="form-select" name="flash_mb" id="flash_mb">
              ${flashOptions}
            </select>
          </div>
          <div class="mb-3">
            <label class="form-label">Placa / nota (opcional)</label>
            <input class="form-control" name="board" maxlength="64" placeholder="esp8285, wroom-32e, …">
          </div>
          <div class="mb-3">
            <label class="form-label">Nombre release</label>
            <input class="form-control" name="name" required maxlength="255" placeholder="USLvlSensor">
          </div>
          <div class="row mb-3">
            <div class="col">
              <label class="form-label">Major</label>
              <input class="form-control" name="major" type="number" min="0" max="99" required value="1">
            </div>
            <div class="col">
              <label class="form-label">Minor</label>
              <input class="form-control" name="minor" type="number" min="0" max="99" required value="0">
            </div>
            <div class="col">
              <label class="form-label">Patch</label>
              <input class="form-control" name="patch" type="number" min="0" max="99" required value="0">
            </div>
          </div>
          <div class="mb-3">
            <label class="form-label">Archivo .bin</label>
            <input class="form-control" type="file" name="binary_file" accept=".bin" required>
          </div>
          <button type="submit" class="btn btn-primary">Guardar</button>
        </form>
      </div>
    </div>
    <div class="card">
      <div class="card-header">Publicados</div>
      <div class="table-responsive">
        <table class="table table-striped mb-0 table-sm">
          <thead><tr><th>ID</th><th>Plat.</th><th>Layout</th><th>Placa</th><th>Nombre</th><th>Versión</th><th>Usuario</th><th>Fecha</th></tr></thead>
          <tbody>${rows || '<tr><td colspan="8" class="text-muted">Sin releases</td></tr>'}</tbody>
        </table>
      </div>
    </div>
  </div>
  <script>
    const platform = document.getElementById('platform');
    const layoutBlock = document.getElementById('layoutBlock');
    const flashBlock = document.getElementById('flashBlock');
    const fsLayout = document.getElementById('fs_ota_layout');
    function syncPlatform() {
      const is8266 = platform.value === 'esp8266';
      layoutBlock.classList.toggle('d-none', !is8266);
      flashBlock.classList.toggle('d-none', is8266);
      fsLayout.required = is8266;
    }
    platform.addEventListener('change', syncPlatform);
    syncPlatform();
  </script>
</body>
</html>`;
}

module.exports = { createUploadRouter };