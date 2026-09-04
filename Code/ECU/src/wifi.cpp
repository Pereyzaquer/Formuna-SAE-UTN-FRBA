/************************************************************
 *  Proyecto : ECU
 *  Archivo  : wifi.cpp
 *  Equipo   : UTN BA Motorsport Formula student team
 *  Fecha    : 2/9/2026
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Punto de acceso wifi de la ECU, actualizacion del firmware
 *  por aire, y atencion del servidor web de monitoreo.
 *
 *  Hardware:
 *  --------------------------------------------------------
 *  - MCU: ESP32 S3.
 *  - Sensores: ninguno propio.
 *
 *  Notas:
 *  --------------------------------------------------------
 *  La ECU levanta su propio punto de acceso porque en el box no
 *  hay ninguna red a la que conectarse. Quien decide cuando la
 *  radio esta encendida es la maquina de estados: aca solo se
 *  obedecen los comandos que llegan por queueWifi.
 *
 ************************************************************/

/************************************************************
 *                     INCLUDES
 ************************************************************/
#include "../include/ECU.h"

/************************************************************
 *               CONSTANTES DEL SISTEMA
 ************************************************************/
static const uint32_t WIFI_TASK_PERIOD_MILLISECONDS = 20;

/************************************************************
 *             PROTOTIPOS DE FUNCIONES LOCALES
 ************************************************************/
static void startWifiAndServices(void);
static void stopWifiAndServices(void);

/**
 * @brief Task de wifi: punto de acceso, actualizacion OTA y web.
 *
 * Los comandos repetidos se ignoran, asi la maquina de estados puede
 * mandar el suyo en cada transicion sin llevar la cuenta de si la radio
 * ya estaba como se pide.
 *
 * @param[in]  argument  No se usa. Lo exige la firma de FreeRTOS.
 *
 * @return void
 */
void taskWifi(void *argument) {
  (void)argument;

  startWifiAndServices();
  bool servicesAreRunning = true;

  for (;;) {
    WifiCommand command;
    if (xQueueReceive(queueWifi, &command, 0) == pdTRUE) {
      if (command == WifiCommand::DISABLE && servicesAreRunning) {
        stopWifiAndServices();
        servicesAreRunning = false;
      } else if (command == WifiCommand::ENABLE && !servicesAreRunning) {
        startWifiAndServices();
        servicesAreRunning = true;
      }
    }

    if (servicesAreRunning) {
      ArduinoOTA.handle();
      webMonitorHandleRequests();
    }

    vTaskDelay(pdMS_TO_TICKS(WIFI_TASK_PERIOD_MILLISECONDS));
  }
}

/************************************************************
 *                  FUNCIONES LOCALES
 ************************************************************/

/**
 * @brief Levanta el punto de acceso, el OTA y el servidor web.
 *
 * @return void
 */
static void startWifiAndServices(void) {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_NETWORK_NAME, WIFI_PASSWORD);

  /* En modo punto de acceso la direccion util es la del softAP:
     localIP() devuelve 0.0.0.0 porque esta placa no se conecta a
     ninguna red, la crea. Se imprime siempre, porque es el unico dato
     con el que alguien del equipo encuentra la ECU. */
  Serial.print("Red wifi: ");
  Serial.println(WIFI_NETWORK_NAME);
  Serial.print("Direccion IP: ");
  Serial.println(WiFi.softAPIP());

  ArduinoOTA.setHostname("esp32-ota");
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.begin();

  webMonitorStart();
}

/**
 * @brief Apaga el servidor web, el OTA y la radio.
 *
 * @return void
 */
static void stopWifiAndServices(void) {
  webMonitorStop();
  ArduinoOTA.end();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);

  Serial.println("Wifi apagado");
}
