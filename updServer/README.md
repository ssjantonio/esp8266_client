# Servidor OTA ESP8266 (`updServer`)

Servicio HTTP ligero (Node.js + nginx) compatible con `ESP8266httpUpdate` y panel de carga de firmware.

## Endpoints

| Ruta | Auth | Uso |
|------|------|-----|
| `GET /update/esp8266` | No | OTA desde el sketch (`AutoUpdate.h`) |
| `GET/POST /upload-esp8266` | Basic (nginx) | Listar y subir `.bin` |

La lógica replica el controlador Laravel `UpdateESP8266Controller` (cabeceras `ESP8266-http-Update`, `x-esp8266-version`, `x-esp8266-mode`).

Metadatos en `data/releases.json`; binarios en `storage/bin/esp8266/`.

## Requisitos en el servidor

- Ubuntu (probado en `192.168.17.159`)
- Node.js ≥ 18
- nginx + `apache2-utils` (`htpasswd`)

## Configuración inicial (una vez)

1. Clonar o copiar el repo en el servidor, por ejemplo en `/home/antonio/esp8266-upd-server`.
2. Desde su PC (con SSH al servidor):

   ```powershell
   cd "...\USLvlSensor\updServer\scripts"
   .\deploy.ps1 -FirstInstall
   ```

   O en Linux/Git Bash:

   ```bash
   ./deploy.sh
   ```

3. En el servidor, ejecutar el instalador:

   Interactivo (pide usuario/contraseña para nginx):

   ```bash
   ssh antonio@192.168.17.159
   bash ~/esp8266-upd-server/updServer/scripts/install-on-server.sh
   ```

   Sin preguntas (defina la contraseña del panel; no la suba a git):

   ```bash
   ADMIN_PASS='su-contraseña' bash ~/esp8266-upd-server/updServer/scripts/bootstrap-server-noninteractive.sh
   ```

4. Crear o revisar `updServer/.env` (no va a git). En producción deje `REQUIRE_APP_AUTH=false` si nginx protege `/upload-esp8266`.

5. En el firmware, URL OTA:

   ```cpp
   const char * updateUrl = "http://192.168.17.159/update/esp8266";
   ```

   La versión del sketch (`TRUC_VERSION`) debe coincidir con la subida en el panel.

## Despliegues siguientes

```powershell
.\deploy.ps1
```

```bash
./deploy.sh
```

Reinicia `esp8266-upd` si el servicio systemd ya existe.

## Comprobaciones

```bash
curl -s http://192.168.17.159/health
# OTA sin auth (debe responder JSON 304 o error controlado sin Basic):
curl -s -o /dev/null -w "%{http_code}" -H "User-Agent: ESP8266-http-Update" \
  -H "x-esp8266-mode: sketch" -H "x-esp8266-version: 0_0_0" \
  http://192.168.17.159/update/esp8266
```

Panel: abrir `http://192.168.17.159/upload-esp8266` (usuario definido en `install-on-server.sh`).

## Servicios

- `sudo systemctl status esp8266-upd`
- `sudo nginx -t && sudo systemctl reload nginx`

Logs: `journalctl -u esp8266-upd -f`