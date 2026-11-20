function releaseMatchKind(release, ota) {
  const layout = release.fs_ota_layout || '';
  if (layout === 'ANY') return 'any';
  if (layout && ota.fsOtaLayout && layout === ota.fsOtaLayout) return 'layout';
  if (layout) return 'layout';
  if (!layout && release.flash_mb) return 'legacy_flash_mb';
  return 'universal';
}

function releaseLayoutLabel(release) {
  if (release.fs_ota_layout) return release.fs_ota_layout;
  if (release.flash_mb) return `legacy_flash_${release.flash_mb}M`;
  return 'universal';
}

module.exports = { releaseMatchKind, releaseLayoutLabel };