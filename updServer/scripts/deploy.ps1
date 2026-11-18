# Despliegue desde Windows hacia el servidor OTA
param(
  [string]$SshHost = "antonio@192.168.17.159",
  [string]$RemoteRoot = "/home/antonio/esp8266-upd-server",
  [switch]$FirstInstall
)

$ErrorActionPreference = "Stop"
$UpdServerDir = Split-Path -Parent $PSScriptRoot
$ProjectRoot = Split-Path -Parent $UpdServerDir

$items = @(
  "updServer",
  "USLvlSensor.ino",
  "AutoUpdate.h",
  "ConfigJson.cpp",
  "ConfigJson.h",
  "SensorData.cpp",
  "SensorData.h",
  "ModbusTcpSlave.cpp",
  "ModbusTcpSlave.h",
  ".gitignore",
  "README.md",
  "Config.h.example"
)

ssh $SshHost "mkdir -p $RemoteRoot"

foreach ($item in $items) {
  $src = Join-Path $ProjectRoot $item
  if (-not (Test-Path $src)) { continue }
  scp -r $src "${SshHost}:${RemoteRoot}/"
}

$remoteCmd = "set -e; cd ${RemoteRoot}/updServer; sed -i 's/\r$//' scripts/*.sh deploy/*.conf 2>/dev/null || true; npm install --omit=dev; mkdir -p data storage/bin/esp8266; test -f data/releases.json || cp data/releases.example.json data/releases.json; if [ ! -f .env ]; then cp .env.example .env; sed -i 's/^REQUIRE_APP_AUTH=.*/REQUIRE_APP_AUTH=false/' .env; fi; if systemctl is-active --quiet esp8266-upd.service 2>/dev/null; then sudo systemctl restart esp8266-upd.service; fi"
ssh $SshHost $remoteCmd

if ($FirstInstall) {
  Write-Host "Ejecute en el servidor: bash $RemoteRoot/updServer/scripts/install-on-server.sh"
  Write-Host "O con SSH interactivo: ssh $SshHost 'bash $RemoteRoot/updServer/scripts/install-on-server.sh'"
}

Write-Host "Deploy sincronizado en ${SshHost}:${RemoteRoot}"