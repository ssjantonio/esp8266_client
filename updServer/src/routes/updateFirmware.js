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
const { releaseMatchKind, releaseLayoutLabel } = require('../lib/releaseMatch');

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

      const match = releaseMatchKind(release, ota);
      const releaseLayout = releaseLayoutLabel(release);
      const binStat = fs.statSync(resolved);

      const releaseMeta = {
        version: release.version,
        release_id: release.id,
        release_name: release.name,
        fs_ota_layout: release.fs_ota_layout || null,
        release_layout: releaseLayout,
        flash_mb: release.flash_mb || null,
        binary_bytes: binStat.size,
        binary_file: path.basename(resolved),
        match,
        client_fs_ota_layout: ota.fsOtaLayout || null,
        platform: detected.platform,
      };

      if (ota.action === 'check') {
        log(
          'Update available:',
          req.ip,
          release.version,
          releaseLayout,
          'match',
          match,
          binStat.size,
          'bytes'
        );
        return res.status(200).json({
          result: 'update',
          ...releaseMeta,
        });
      }

      log(
        'Download',
        detected.platform,
        release.version,
        releaseLayout,
        'match',
        match,
        binStat.size,
        'bytes',
        'client',
        ota.fsOtaLayout,
        req.ip
      );

      res.setHeader('X-HQ-Release-Version', release.version);
      res.setHeader('X-HQ-Release-Id', String(release.id));
      res.setHeader('X-HQ-Release-Name', release.name || '');
      res.setHeader('X-HQ-Release-Layout', releaseLayout);
      res.setHeader('X-HQ-Release-Match', match);
      res.setHeader('X-HQ-Client-Layout', ota.fsOtaLayout || '');
      res.setHeader('X-HQ-Binary-Bytes', String(binStat.size));

      return res.download(resolved, path.basename(resolved));
    } catch (err) {
      log('update error', err);
      return res.status(304).json({ result: 'No updates', data: '' });
    }
  });

  return router;
}

module.exports = { createUpdateRouter };