# Servidor OTA (`updServer`)

Servicio HTTP ligero (Node.js + nginx) compatible con **ESP8266httpUpdate** y **ESP32 HTTPUpdate**, más panel de carga.

## Endpoints

| Ruta | Auth | Uso |
|------|------|-----|
| `GET /update/esp8266` | No | OTA ESP8266/8285 (`User-Agent: ESP8266-http-Update`) |
| `GET /update/esp32` | No | OTA ESP32 (`User-Agent: ESP32-http-Update`) |
| `GET/POST /upload-esp8266` | Basic (nginx) | Listar y subir `.bin` (8266 y 32) |

## Cómo elige el binario

### ESP8266 — por layout FS/OTA (`fs_ota_layout`)

El dispositivo envía (además de `x-*-version`):

| Cabecera | Origen |
|----------|--------|
| `x-ESP8266-chip-size` | Flash real (bytes) |
| `x-ESP8266-free-space` | Espacio OTA disponible (~tamaño slot) |
| `x-ESP8266-fs-partition-bytes` | Partición FS del build (`FS_PHYS_SIZE`) |

El servidor **infiere** un id de layout (p. ej. `1M64K512K`, `4M2M1M`) y busca en `releases.json` un release con el **mismo** `fs_ota_layout` y versión **mayor** que la del equipo.

Convención del id: flash total + FS + OTA, como en Arduino IDE — `1M64K512K` = 1 MB (FS 64 KB, OTA ~512 KB).

Catálogo fijo en `src/lib/esp8266Layouts.js` (lista del panel `/upload-esp8266`).

| Campo en release | Significado |
|----------------|-------------|
| `fs_ota_layout` | Id del layout (`1M64K512K`, `4M2M1M`, …) o `ANY` |
| `flash_mb` | Derivado del layout (informativo / ESP32 legacy) |
| `board` | Nota libre |

Reglas:

1. Misma versión más nueva → release con **`fs_ota_layout` coincidente**.
2. Si no hay match, **no** se sirve otro layout (evita cruzar 1M64 con 4M2M).
3. Excepción: release `fs_ota_layout: "ANY"` (universal, usar con cuidado).
4. **Legacy `flash_mb`** (sin `fs_ota_layout`): si no hay bin del layout inferido, se usa release con el mismo `flash_mb` (p. ej. `1`) — útil mientras la flota no trae cabeceras nuevas. En el panel: *Legacy 1 MB (solo flash_mb)*. Prioridad: layout exacto → legacy flash → `ANY`.

Cabecera `x-esp8266-action: check` → `200` JSON sin bajar el `.bin`.

### ESP32

Sigue `flash_mb` en el upload (sin layout FS/OTA en 8266).

Metadatos en `data/releases.json`; binarios en `storage/bin/esp8266/` y `storage/bin/esp32/`.

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
  -H "x-esp8266-chip-size: 1048576" -H "x-esp8266-free-space: 489000" \
  -H "x-esp8266-fs-partition-bytes: 65536" \
  http://192.168.17.159/update/esp8266
```

Panel: abrir `http://192.168.17.159/upload-esp8266` (usuario definido en `install-on-server.sh`).

## Servicios

- `sudo systemctl status esp8266-upd`
- `sudo nginx -t && sudo systemctl reload nginx`

Logs: `journalctl -u esp8266-upd -f`