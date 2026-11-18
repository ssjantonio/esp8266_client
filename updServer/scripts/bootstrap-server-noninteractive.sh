#!/usr/bin/env bash
# Primera instalación sin preguntas (ejecutar en el servidor con sudo donde aplique).
# Uso: ADMIN_PASS='secret' ./bootstrap-server-noninteractive.sh
set -euo pipefail

APP_USER="${APP_USER:-antonio}"
INSTALL_ROOT="/home/${APP_USER}/esp8266-upd-server"
UPD_DIR="${INSTALL_ROOT}/updServer"
NGINX_USER="${NGINX_UPLOAD_USER:-admin}"
NGINX_PASS="${ADMIN_PASS:?Defina ADMIN_PASS}"

sudo apt-get update -qq
sudo apt-get install -y nginx apache2-utils

cd "${UPD_DIR}"
npm install --omit=dev
mkdir -p data storage/bin/esp8266
test -f data/releases.json || cp data/releases.example.json data/releases.json
if [[ ! -f .env ]]; then
  cp .env.example .env
  sed -i 's/^REQUIRE_APP_AUTH=.*/REQUIRE_APP_AUTH=false/' .env
fi

echo "${NGINX_PASS}" | sudo htpasswd -ci /etc/nginx/esp8266-upd.htpasswd "${NGINX_USER}"

sudo cp deploy/nginx-esp8266-upd.conf /etc/nginx/sites-available/esp8266-upd
sudo ln -sf /etc/nginx/sites-available/esp8266-upd /etc/nginx/sites-enabled/esp8266-upd
sudo rm -f /etc/nginx/sites-enabled/default 2>/dev/null || true
sudo nginx -t
sudo systemctl enable nginx
sudo systemctl reload nginx

sudo cp deploy/esp8266-upd.service /etc/systemd/system/esp8266-upd.service
sudo sed -i "s|User=antonio|User=${APP_USER}|" /etc/systemd/system/esp8266-upd.service
sudo systemctl daemon-reload
sudo systemctl enable --now esp8266-upd.service

echo OK