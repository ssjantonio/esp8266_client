#!/usr/bin/env bash
# Ejecutar EN el servidor (192.168.17.159) la primera vez, como usuario con sudo.
set -euo pipefail

APP_USER="${APP_USER:-antonio}"
APP_HOME="/home/${APP_USER}"
INSTALL_ROOT="${APP_HOME}/esp8266-upd-server"
UPD_DIR="${INSTALL_ROOT}/updServer"
NGINX_SITE="esp8266-upd"

echo "==> Instalando dependencias del sistema (nginx, apache2-utils)..."
sudo apt-get update -qq
sudo apt-get install -y nginx apache2-utils

if [[ ! -d "${UPD_DIR}" ]]; then
  echo "Error: no existe ${UPD_DIR}. Sincronice el repo antes (scripts/deploy.sh)."
  exit 1
fi

cd "${UPD_DIR}"

if [[ ! -f .env ]]; then
  echo "==> Creando .env desde .env.example — EDITE ADMIN_PASSWORD si usa REQUIRE_APP_AUTH=true"
  cp .env.example .env
  REQUIRE_APP_AUTH=false
  if ! grep -q '^REQUIRE_APP_AUTH=' .env; then
    echo 'REQUIRE_APP_AUTH=false' >> .env
  fi
fi

echo "==> npm install..."
npm install --omit=dev

mkdir -p data storage/bin/esp8266
if [[ ! -f data/releases.json ]]; then
  cp data/releases.example.json data/releases.json
fi

echo "==> Usuario/contraseña HTTP Basic para /upload-esp8266"
read -r -p "Usuario nginx [admin]: " HT_USER
HT_USER="${HT_USER:-admin}"
read -r -s -p "Contraseña: " HT_PASS
echo
sudo htpasswd -bc "/etc/nginx/esp8266-upd.htpasswd" "${HT_USER}" "${HT_PASS}"

echo "==> Nginx site"
sudo cp deploy/nginx-esp8266-upd.conf "/etc/nginx/sites-available/${NGINX_SITE}"
sudo ln -sf "/etc/nginx/sites-available/${NGINX_SITE}" "/etc/nginx/sites-enabled/${NGINX_SITE}"
sudo rm -f /etc/nginx/sites-enabled/default 2>/dev/null || true
sudo nginx -t
sudo systemctl reload nginx

echo "==> systemd"
sudo cp deploy/esp8266-upd.service /etc/systemd/system/esp8266-upd.service
sudo sed -i "s|User=antonio|User=${APP_USER}|g" /etc/systemd/system/esp8266-upd.service
sudo sed -i "s|/home/antonio/esp8266-upd-server|${INSTALL_ROOT}|g" /etc/systemd/system/esp8266-upd.service
sudo systemctl daemon-reload
sudo systemctl enable --now esp8266-upd.service

echo "Listo. Pruebe: curl -s http://127.0.0.1/health"
echo "OTA:     http://192.168.17.159/update/esp8266"
echo "Upload:  http://192.168.17.159/upload-esp8266"