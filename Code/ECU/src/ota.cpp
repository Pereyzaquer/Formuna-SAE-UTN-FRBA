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
 *  La ECU levanta su propio AP: en el box no hay red a la que
 *  conectarse. El AP queda disponible en todos los estados salvo
 *  CAR_ON, asi se puede actualizar la ECU montada en el auto sin
 *  desarmar nada. La maquina de estados es la que decide: aca solo
 *  se obedecen los comandos que llegan por qOta.
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

  WiFi.mode(WIFI_AP);
  WiFi.softAP(SSID, PASSWORD);

  /* En modo AP la IP util es la del softAP: localIP() devuelve 0.0.0.0
     porque no hay ninguna red a la que esta placa se haya conectado.
     Se imprime siempre: es el unico dato con el que alguien del equipo
     puede encontrar la ECU para actualizarla por wifi. */
  Serial.print("AP: ");
  Serial.println(SSID);
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());

  ArduinoOTA.setHostname("esp32-ota");
  ArduinoOTA.setPassword(OTA_PASSWORD);

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

/**
 * @brief Task de OTA: atiende actualizaciones mientras esten habilitadas.
 *
 * Se prende y se apaga por comando de la maquina de estados. Los comandos
 * repetidos se ignoran, asi que enterState() puede mandar el suyo en cada
 * transicion sin llevar la cuenta de si el wifi ya estaba como se pide.
 *
 * @param[in]  arg  No se usa. Requerido por la firma de FreeRTOS.
 *
 * @return void
 */
void taskOta(void *arg) {
  (void)arg;

  otaSetup();
  webmonBegin();
  bool enabled = true;

  for (;;) {
    OtaCmd cmd;
    if (xQueueReceive(qOta, &cmd, 0) == pdTRUE) {
      if (cmd == OtaCmd::DISABLE && enabled) {
        /* El monitoreo web se baja junto con el wifi: sin radio no tiene
           a quien contestarle, y dejarlo escuchando solo gasta memoria. */
        webmonStop();
        ArduinoOTA.end();
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_OFF);
        enabled = false;
      } else if (cmd == OtaCmd::ENABLE && !enabled) {
        otaSetup();
        webmonBegin();
        enabled = true;
      }
    }

    if (enabled) {
      ArduinoOTA.handle();
      webmonHandle();
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
