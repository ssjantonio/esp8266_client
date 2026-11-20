require('dotenv').config();
const path = require('path');
const express = require('express');
const basicAuth = require('express-basic-auth');
const { ensureDataFile } = require('./lib/releases');
const { createUpdateRouter } = require('./routes/updateFirmware');
const { createUploadRouter } = require('./routes/uploadEsp8266');

const PORT = parseInt(process.env.PORT || '3010', 10);
const appRoot = path.join(__dirname, '..');
const dataDir = path.resolve(appRoot, process.env.DATA_DIR || './data');
const storageDir = path.resolve(appRoot, process.env.STORAGE_DIR || './storage');

const useAppAuth = process.env.REQUIRE_APP_AUTH === 'true';
const adminUser = process.env.ADMIN_USER || 'admin';
const adminPassword = process.env.ADMIN_PASSWORD || '';
if (useAppAuth && !adminPassword) {
  console.error('Defina ADMIN_PASSWORD cuando REQUIRE_APP_AUTH=true');
  process.exit(1);
}

ensureDataFile(dataDir);

const log = (...args) => console.log(new Date().toISOString(), ...args);

const app = express();
app.set('trust proxy', true);
app.use(express.urlencoded({ extended: true }));

const authMiddleware = useAppAuth
  ? basicAuth({
      users: { [adminUser]: adminPassword },
      challenge: true,
      realm: 'ESP8266 upload',
    })
  : (_req, _res, next) => next();

const updateOpts = { dataDir, appRoot, log };
app.use(
  '/update/esp8266',
  createUpdateRouter({ ...updateOpts, platform: 'esp8266' })
);
app.use(
  '/update/esp32',
  createUpdateRouter({ ...updateOpts, platform: 'esp32' })
);
app.use(
  '/upload-esp8266',
  createUploadRouter({ dataDir, storageDir, basicAuth: authMiddleware })
);

app.get('/health', (_req, res) => {
  res.type('text').send('ok');
});

app.listen(PORT, '127.0.0.1', () => {
  log(`Listening on 127.0.0.1:${PORT}`);
});