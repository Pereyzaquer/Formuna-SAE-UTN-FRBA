/************************************************************
 *  Proyecto : ECU
 *  Archivo  : main.cpp
 *  Equipo   : UTN BA Motorsport Formula student team
 *  Fecha    : 2/9/2026
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Computadora central de administracion de telemetria de
 *  multiples sensores y estado del vehiculo.
 *
 *  Hardware:
 *  --------------------------------------------------------
 *  - MCU: ESP32 S3.
 *  - Sensores: ninguno propio. Los nodos ESP32-C3 reportan
 *    por CAN.
 *
 *  Notas:
 *  --------------------------------------------------------
 *  Este archivo solo arma el sistema: inicializa el hardware y
 *  crea las tasks. La logica vive en los demas archivos.
 *
 *  Reparto de nucleos: el 1 corre lo que tiene que ser
 *  predecible, estados y CAN; el 0 queda para el wifi, que es
 *  donde el stack de radio ya corre sus propias tasks.
 *
 ************************************************************/

/************************************************************
 *                     INCLUDES
 ************************************************************/
#include "../include/ECU.h"

/************************************************************
 *               CONSTANTES DEL SISTEMA
 ************************************************************/
static const uint16_t STACK_SIZE_STATE = 4096;
static const uint16_t STACK_SIZE_CAN   = 4096;
static const uint16_t STACK_SIZE_WIFI  = 8192; /**< El servidor web necesita mas. */

/* El bus no puede perder tramas esperando a la maquina de estados, por
   eso la task de CAN tiene mas prioridad que la de estados. */
static const uint8_t PRIORITY_CAN   = 4;
static const uint8_t PRIORITY_STATE = 3;
static const uint8_t PRIORITY_WIFI  = 1;

static const uint8_t CORE_REALTIME = 1;
static const uint8_t CORE_WIRELESS = 0;

static const uint8_t QUEUE_LENGTH_EVENTS = 16;
static const uint8_t QUEUE_LENGTH_WIFI   = 4;

/************************************************************
 *                VARIABLES GLOBALES
 ************************************************************/
Adafruit_NeoPixel rgbLed(RGB_LED_COUNT, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);

QueueHandle_t queueEvents = nullptr;
QueueHandle_t queueWifi   = nullptr;

/************************************************************
 *                       SETUP
 ************************************************************/
void setup() {
  Serial.begin(115200);
  delay(1000);

  indicatorsInitialize();

  queueEvents = xQueueCreate(QUEUE_LENGTH_EVENTS, sizeof(EcuEvent));
  queueWifi   = xQueueCreate(QUEUE_LENGTH_WIFI, sizeof(WifiCommand));

  /* Sin bus CAN la ECU esta ciega: no hay nada util que hacer, asi que
     se queda en rojo fijo en lugar de arrancar una maquina de estados
     que nunca va a recibir un dato. */
  if (!canInitialize()) {
    Serial.println("Error: no se pudo inicializar el bus CAN");
    indicatorsSetColor(64, 0, 0);
    return;
  }

  xTaskCreatePinnedToCore(taskState, "state", STACK_SIZE_STATE, nullptr,
                          PRIORITY_STATE, nullptr, CORE_REALTIME);
  xTaskCreatePinnedToCore(taskCan, "can", STACK_SIZE_CAN, nullptr,
                          PRIORITY_CAN, nullptr, CORE_REALTIME);
  xTaskCreatePinnedToCore(taskWifi, "wifi", STACK_SIZE_WIFI, nullptr,
                          PRIORITY_WIFI, nullptr, CORE_WIRELESS);
}

/************************************************************
 *                        LOOP
 ************************************************************/
void loop() {
  /* Todo el trabajo vive en las tasks. Se cede el nucleo para no
     gastarle tiempo al planificador sin motivo. */
  vTaskDelay(pdMS_TO_TICKS(1000));
}
