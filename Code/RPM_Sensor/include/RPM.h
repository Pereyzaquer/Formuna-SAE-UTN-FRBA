/************************************************************
 *  Proyecto : RPM_Sensor
 *  Archivo  : RPM.h
 *  Equipo   : UTN BA Motorsport Formula student team
 *  Fecha    : 8/1/2026
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Encabezado principal del codigo
 *
 *  Notas:
 *  --------------------------------------------------------
 * 
 ************************************************************/

/************************************************************
 *                     INCLUDES
 ************************************************************/
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <Adafruit_NeoPixel.h>

/************************************************************
 *                     DEFINES
 ************************************************************/
#define SSID            "RPM_F_SENSOR"
#define PASSWORD        "12345678"

#define SCK_PIN         4
#define MISO_PIN        5
#define MOSI_PIN        6
#define CS_PIN          7

/************************************************************
 *             PROTOTIPOS DE FUNCIONES
 ************************************************************/

/**
 * @brief Realiza el setup de la configuracion OTA
 *
 * @return void           
 */
void otaSetup(void);