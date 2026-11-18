const express = require('express');
const path = require('path');
const fs = require('fs');
const multer = require('multer');
const {
  listReleases,
  addRelease,
  parseNumericVersion,
} = require('../lib/releases');

const VERSION_RE = /^\d{1,3}_\d{1,3}_\d{1,3}$/;

function createUploadRouter({ dataDir, storageDir, basicAuth }) {
  const router = express.Router();
  router.use(basicAuth);

  const binEsp = path.join(storageDir, 'bin', 'esp8266');
  fs.mkdirSync(binEsp, { recursive: true });

  const storage = multer.diskStorage({
    destination: (_req, _file, cb) => cb(null, binEsp),
    filename: (_req, file, cb) => {
      const safe = file.originalname.replace(/[^a-zA-Z0-9._-]/g, '_');
      cb(null, `${Date.now()}_${safe}`);
    },
  });

  const upload = multer({
    storage,
    limits: { fileSize: 1024 * 1024 },
    fileFilter: (_req, file, cb) => {
      if (!file.originalname.toLowerCase().endsWith('.bin')) {
        return cb(new Error('Solo archivos .bin'));
      }
      cb(null, true);
    },
  });

  router.get('/', (_req, res) => {
    const releases = listReleases(dataDir).filter((r) => r.type === 'esp8266');
    res.type('html').send(renderPage(releases));
  });

  router.post('/', upload.single('binary_file'), (req, res) => {
    const name = (req.body.name || '').trim();
    const version = (req.body.version || '').trim();
    const type = 'esp8266';

    if (!name || !VERSION_RE.test(version)) {
      return res.status(400).send(errorPage('Nombre y versión (X_Y_Z) son obligatorios.'));
    }
    if (!req.file) {
      return res.status(400).send(errorPage('Falta el archivo .bin (máx. 1 MB).'));
    }

    const numeric_version = parseNumericVersion(version);
    if (numeric_version === null) {
      return res.status(400).send(errorPage('Versión inválida.'));
    }

    const relativePath = path.join('storage', 'bin', type, req.file.filename).replace(/\\/g, '/');
    const user = req.auth?.user || 'admin';

    addRelease(dataDir, {
      name,
      user,
      version,
      type,
      numeric_version,
      binary_file: relativePath,
    });

    res.redirect('/upload-esp8266');
  });

  return router;
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
  const rows = releases
    .map(
      (b) => `<tr>
        <td>${b.id}</td>
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
  <title>Subir firmware ESP8266</title>
  <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/css/bootstrap.min.css" rel="stylesheet">
</head>
<body class="bg-light">
  <div class="container py-4">
    <h1 class="h3 mb-4">Firmware ESP8266</h1>
  <p class="text-muted">Versión en el sketch debe coincidir con <code>TRUC_VERSION</code> en <code>AutoUpdate.h</code> (formato <code>1_0_4</code>).</p>
    <div class="card mb-4">
      <div class="card-header">Nueva versión</div>
      <div class="card-body">
        <form method="post" enctype="multipart/form-data" action="/upload-esp8266">
          <div class="mb-3">
            <label class="form-label">Nombre</label>
            <input class="form-control" name="name" required maxlength="255" placeholder="USLvlSensor release">
          </div>
          <div class="mb-3">
            <label class="form-label">Versión</label>
            <input class="form-control" name="version" required pattern="\\d{1,3}_\\d{1,3}_\\d{1,3}" placeholder="1_0_4">
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
        <table class="table table-striped mb-0">
          <thead><tr><th>ID</th><th>Nombre</th><th>Versión</th><th>Usuario</th><th>Fecha</th></tr></thead>
          <tbody>${rows || '<tr><td colspan="5" class="text-muted">Sin releases</td></tr>'}</tbody>
        </table>
      </div>
    </div>
  </div>
</body>
</html>`;
}

module.exports = { createUploadRouter };