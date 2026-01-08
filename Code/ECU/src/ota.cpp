/************************************************************
 *  Proyecto : ECU
 *  Archivo  : ota.cpp
 *  Equipo   : UTN BA Motorsport Formula student team 
 *  Fecha    : 8/1/2026
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Funciones de seteo de la comunicacion y programacion
 *  por wifi del modulo
 *
 *  Hardware:
 *  --------------------------------------------------------
 *  - MCU: ESP32 S3.
 *  - Sensores: 
 *
 *  Notas:
 *  --------------------------------------------------------
 *
 ************************************************************/

/************************************************************
 *                     INCLUDES
 ************************************************************/
#include "../include/ECU.h"

/************************************************************
 *                VARIABLES GLOBALES EXTERNAS
 ************************************************************/

/**
 * @brief Realiza el setup de la configuracion OTA
 *
 * @return void           
 */
void otaSetup(void) {

  //Serial.println("Iniciando WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.softAP(SSID, PASSWORD);

  //Serial.println("\nWiFi conectado");
  //Serial.print("IP: ");
  //Serial.println(WiFi.localIP());

  ArduinoOTA.setHostname("esp32-ota");
  // ArduinoOTA.setPassword("1234");

  ArduinoOTA.onStart([]() {
    //Serial.println("Iniciando OTA");
  });

  ArduinoOTA.onEnd([]() {
    //Serial.println("\nOTA finalizado");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    //Serial.printf("Progreso: %u%%\r", (progress * 100) / total);
  });

  ArduinoOTA.onError([](ota_error_t error) {
    //Serial.printf("Error OTA [%u]\n", error);
  });

  ArduinoOTA.begin();

  //Serial.println("OTA listo");
}