#pragma once 
#define AUTO_UPDATE_H
#include <ESP8266httpUpdate.h>

// Tiene que ser exactamente la versión con la que se sube el .bin en /arduino-upload.
// Si el texto de aquí es menor que el registro del servidor, el equipo se reprograma en bucle.
const char * TRUC_VERSION = "1_0_4";
const char * SPIFFS_VERSION = "0_1_0";

const char * updateUrl="http://192.168.17.159/update/esp8266";


t_httpUpdate_return actualUpdate(bool sketch=false){
    String msg;
    t_httpUpdate_return ret;
    WiFiClient client;
    

    ESPhttpUpdate.rebootOnUpdate(false);

    Serial.printf("Find updates on %s\n", updateUrl);

    if(sketch){
      return ESPhttpUpdate.update(client, updateUrl, TRUC_VERSION);     // **************** This is the line that "does the business"   
    }
    else {
      return ESPhttpUpdate.updateFS(client, updateUrl, SPIFFS_VERSION);
    }
  
}