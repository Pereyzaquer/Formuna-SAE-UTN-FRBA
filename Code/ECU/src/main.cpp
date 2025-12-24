/************************************************************
 *  Proyecto : ECU
 *  Archivo  : main.cpp
 *  Equipo   : UTN BA Motorsport Formula student team 
 *  Fecha    : 23/12/2025
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Computadora central de adminstracion de telemetria de
 *  multiples sensores y estado del vehiculo
 *
 *  Hardware:
 *  --------------------------------------------------------
 *  - MCU: ESP32 S3 / ESP32 mini.
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
 *               CONSTANTES DEL SISTEMA
 ************************************************************/
const char* ssid     = "ESP32";
const char* password = "12345678";

/************************************************************
 *                VARIABLES GLOBALES
 ************************************************************/
uint32_t ESTADO = BOOT;
Adafruit_NeoPixel rgb(NUM_PIXELS, RGB_PIN, NEO_GRB + NEO_KHZ800);

/************************************************************
 *                       SETUP
 ************************************************************/
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("Iniciando WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.softAP(ssid, password);

  Serial.println("\nWiFi conectado");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  ArduinoOTA.setHostname("esp32-ota");
  // ArduinoOTA.setPassword("1234");

  ArduinoOTA.onStart([]() {
    Serial.println("Iniciando OTA");
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA finalizado");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progreso: %u%%\r", (progress * 100) / total);
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error OTA [%u]\n", error);
  });

  ArduinoOTA.begin();

  Serial.println("OTA listo");

  rgb_init();
}

/************************************************************
 *                        LOOP
 ************************************************************/
void loop() {
  ArduinoOTA.handle();

  switch (ESTADO)
    {
    case BOOT:
        // Chequeo del bus CAN
        // Chequeo del driver (junto con el estado del mismo)
        // Chequeo de los sensores de la bateria (junto con el estado de la misma)
        // Chequeo de los sensores de RPM de las ruedas
        // Chequeo del sensor del volante
        // Chequeo del sensor del acelerador
        // Chequeo del sensor del freno
        // Chequeo del sensor mpu6050 (giroscopio y acelerometro)
        // Chequeo del sensor de precion y temperatura de los neumaticos*

        // Todos los chequeos deben setear una flag para determinar si se configurará el sensor o no en el siguient estado.
        // El caso del driver y la bateria, no debe de haber flag dado que no puede bootearse el esp si no esta alguno de los dos 
        // Para las pruebas vamos a excluir esto pero en un futuro deberiamos considerar que el driver y la bateria causen el estado
        // de falla en el estado BOOT si no se comunican.

        break;
    case CONFIG:
        
        // Configuracion de los sensores detectados en el anterior estado

        break;
    case CAR_READY:
        
        // Seteo de los leds indicados por reglamento del estado del vehiculo

        break;
    case CAR_ON:
        
        // Seteo de los leds indicados por reglamento del estado del vehiculo y
        // habilitacion del driver para comenzar a funcionar

        break;
    case FAULT:
        
        // Se debe apagar el driver, indicar falla mediante el led indicado por la normativa y apagar o dejar en modo "sleep" a todos los sensores

        break;
    
    default:
        break;
    }
}
