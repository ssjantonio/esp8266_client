const express = require('express');
const fs = require('fs');
const path = require('path');
const { getBestRelease, parseNumericVersion } = require('../lib/releases');
const {
  detectPlatform,
  readOtaRequest,
  isSketchMode,
  SKETCH_MODES,
} = require('../lib/otaClient');

function createUpdateRouter({ dataDir, appRoot, log, platform }) {
  const router = express.Router();

  router.get('/', (req, res) => {
    try {
      const detected = detectPlatform(req, platform);
      if (!detected.ok) {
        if (detected.reason === 'bad_user_agent') {
          log('Bad User Agent:', detected.userAgent);
          return res
            .status(500)
            .json({ result: 'Bad user Agent!', data: '' });
        }
        log('Platform mismatch:', platform, detected.platform);
        return res
          .status(500)
          .json({ result: 'Wrong update URL for this device', data: '' });
      }

      const ota = readOtaRequest(req, detected.meta);
      if (!SKETCH_MODES.has(ota.mode)) {
        log('Unsupported mode:', ota.mode);
        return res
          .status(500)
          .json({ result: 'Unsupported mode', data: ota.mode || '' });
      }
      if (!isSketchMode(ota.mode)) {
        log('FS OTA not implemented:', ota.mode);
        return res.status(304).json({ result: 'No updates', data: '' });
      }

      const intVersion = parseNumericVersion(ota.version);
      if (intVersion === null) {
        return res.status(304).json({ result: 'No updates', data: '' });
      }

      const release = getBestRelease(dataDir, {
        platform: detected.platform,
        clientVersionNum: intVersion,
        flashMb: ota.flashMb,
        fsOtaLayout: ota.fsOtaLayout,
      });

      if (!release) {
        log(
          'No updates for',
          detected.platform,
          ota.version,
          'layout',
          ota.fsOtaLayout || '(sin resolver)',
          'flashMb',
          ota.flashMb
        );
        return res.status(304).json({ result: 'No updates', data: '' });
      }

      const resolved = path.isAbsolute(release.binary_file)
        ? release.binary_file
        : path.resolve(appRoot, release.binary_file);
      if (!fs.existsSync(resolved)) {
        log('Binary missing:', resolved);
        return res.status(304).json({ result: 'No updates', data: '' });
      }

      if (ota.action === 'check') {
        log(
          'Update available:',
          req.ip,
          release.version,
          'layout',
          release.fs_ota_layout || 'any',
          'flash',
          release.flash_mb || 'any'
        );
        return res.status(200).json({
          result: 'update',
          version: release.version,
          fs_ota_layout: release.fs_ota_layout || null,
          flash_mb: release.flash_mb || null,
          platform: detected.platform,
          client_fs_ota_layout: ota.fsOtaLayout || null,
        });
      }

      log(
        'Download',
        detected.platform,
        release.version,
        'layout',
        release.fs_ota_layout || 'any',
        'client_layout',
        ota.fsOtaLayout,
        req.ip
      );
      return res.download(resolved);
    } catch (err) {
      log('update error', err);
      return res.status(304).json({ result: 'No updates', data: '' });
    }
  });

  return router;
}

module.exports = { createUpdateRouter };