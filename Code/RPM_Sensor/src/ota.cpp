/************************************************************
 *  Proyecto : RPM_Sensor
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
 *  - MCU: ESP32 mini.
 *  - Sensores: 
 *
 *  Notas:
 *  --------------------------------------------------------
 *
 ************************************************************/

/************************************************************
 *                     INCLUDES
 ************************************************************/
#include "../include/RPM.h"

/************************************************************
 *                VARIABLES GLOBALES EXTERNAS
 ************************************************************/
extern bool otaRunning;

/**
 * @brief Realiza el setup de la configuracion OTA
 *
 * @return void           
 */
void otaSetup() {

  WiFi.mode(WIFI_AP);  //WIFI_STA
  WiFi.softAP(SSID, PASSWORD);

  ArduinoOTA.setHostname("esp32-ota");
  // ArduinoOTA.setPassword("1234");

  ArduinoOTA.onStart([]() {
    otaRunning = true;
  });

  ArduinoOTA.onEnd([]() {
    otaRunning = false;
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    //Serial.printf("Progreso: %u%%\r", (progress * 100) / total);
  });

  ArduinoOTA.onError([](ota_error_t error) {
    //Serial.printf("Error OTA [%u]\n", error);
  });

  ArduinoOTA.begin();
}