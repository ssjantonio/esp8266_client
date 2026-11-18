#!/usr/bin/env bash
# Despliegue desde Linux/macOS/Git Bash
set -euo pipefail

SSH_HOST="${SSH_HOST:-antonio@192.168.17.159}"
REMOTE_ROOT="${REMOTE_ROOT:-/home/antonio/esp8266-upd-server}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

ssh "${SSH_HOST}" "mkdir -p ${REMOTE_ROOT}"

rsync -avz --delete \
  --exclude 'node_modules' \
  --exclude '.env' \
  --exclude 'data/releases.json' \
  --exclude 'storage/bin/*' \
  --exclude 'build' \
  --exclude '.vs' \
  --exclude '__vm' \
  --exclude 'Config.h' \
  "${PROJECT_ROOT}/updServer" \
  "${PROJECT_ROOT}/USLvlSensor.ino" \
  "${PROJECT_ROOT}/AutoUpdate.h" \
  "${PROJECT_ROOT}/ModbusTcpSlave.cpp" \
  "${PROJECT_ROOT}/ModbusTcpSlave.h" \
  "${PROJECT_ROOT}/ConfigJson.cpp" \
  "${PROJECT_ROOT}/ConfigJson.h" \
  "${PROJECT_ROOT}/SensorData.cpp" \
  "${PROJECT_ROOT}/SensorData.h" \
  "${PROJECT_ROOT}/.gitignore" \
  "${PROJECT_ROOT}/README.md" \
  "${PROJECT_ROOT}/Config.h.example" \
  "${SSH_HOST}:${REMOTE_ROOT}/"

ssh "${SSH_HOST}" bash -s <<REMOTE
set -e
cd "${REMOTE_ROOT}/updServer"
npm install --omit=dev
mkdir -p data storage/bin/esp8266
test -f data/releases.json || cp data/releases.example.json data/releases.json
test -f .env || { cp .env.example .env; sed -i 's/^REQUIRE_APP_AUTH=.*/REQUIRE_APP_AUTH=false/' .env; }
if systemctl is-active --quiet esp8266-upd.service 2>/dev/null; then
  sudo systemctl restart esp8266-upd.service
fi
REMOTE

echo "Listo: ${SSH_HOST}:${REMOTE_ROOT}"