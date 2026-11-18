const express = require('express');
const fs = require('fs');
const path = require('path');
const { getLatestByType } = require('../lib/releases');

function createUpdateRouter({ dataDir, appRoot, log }) {
  const router = express.Router();

  router.get('/', (req, res) => {
    try {
      const userAgent = req.get('user-agent') || '';
      if (userAgent !== 'ESP8266-http-Update') {
        log('Bad User Agent:', userAgent);
        return res.status(500).json({ result: 'Bad user Agent!', data: '' });
      }

      const mode = req.get('x-esp8266-mode');
      if (mode !== 'sketch' && mode !== 'spiffs') {
        log('Unsupported mode:', mode);
        return res.status(500).json({ result: 'Unsupported mode', data: mode || '' });
      }

      const version = req.get('x-esp8266-version') || '';
      const parts = version.split('_');
      if (parts.length !== 3) {
        return res.status(304).json({ result: 'No updates', data: '' });
      }
      const intVersion =
        parseInt(parts[0], 10) * 100 +
        parseInt(parts[1], 10) * 10 +
        parseInt(parts[2], 10);

      const current = getLatestByType(dataDir, 'esp8266');
      if (current && current.numeric_version > intVersion && mode === 'sketch') {
        const resolved = path.isAbsolute(current.binary_file)
          ? current.binary_file
          : path.resolve(appRoot, current.binary_file);
        if (!fs.existsSync(resolved)) {
          log('Binary missing:', resolved);
          return res.status(304).json({ result: 'No updates', data: '' });
        }
        log('Download update for:', req.ip, current.version);
        return res.download(resolved);
      }

      log('No updates for client', version);
      return res.status(304).json({ result: 'No updates', data: '' });
    } catch (err) {
      log('update error', err);
      return res.status(304).json({ result: 'No updates', data: '' });
    }
  });

  return router;
}

module.exports = { createUpdateRouter };