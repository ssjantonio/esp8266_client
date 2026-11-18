# USLvlSensor (ESP8266)

Firmware de transmisor de nivel / peso con cliente TCP `0x32`, Modbus TCP, web de configuración y OTA HTTP.

## Configuración local

1. Copiar `Config.h.example` → `Config.h` y ajustar WiFi, hostname e identificador.
2. Compilar con placa **Generic ESP8285** (o la que use en planta).
3. OTA: editar `updateUrl` y `TRUC_VERSION` en `AutoUpdate.h`.

## Servidor de actualizaciones

Ver [`updServer/README.md`](updServer/README.md) para instalar el servicio Node + nginx en Linux.

## Repositorio

Código público en [github.com/ssjantonio/esp8266_client](https://github.com/ssjantonio/esp8266_client). No se suben contraseñas (`Config.h`), binarios ni `releases.json` del servidor.