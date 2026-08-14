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
 *  - MCU: ESP32 S3.
 *  - Sensores:
 *
 *  Notas:
 *  --------------------------------------------------------
 *  Este archivo solo arma el sistema: inicializa periferico y
 *  crea las tasks. La logica vive en state.cpp, can.cpp,
 *  sensors.cpp, fault.cpp e indicators.cpp.
 *
 *  Reparto de cores: core 1 corre lo que tiene que ser
 *  determinista (estados y CAN); core 0 queda para WiFi/OTA,
 *  que es donde el stack de radio ya corre sus propias tasks.
 *
 ************************************************************/

/************************************************************
 *                     INCLUDES
 ************************************************************/
#include "../include/ECU.h"

/************************************************************
 *               CONSTANTES DEL SISTEMA
 ************************************************************/
static const uint16_t STACK_STATE = 4096;
static const uint16_t STACK_CAN   = 4096;
static const uint16_t STACK_OTA   = 4096;

static const uint8_t QUEUE_EVENTS_LEN = 16;
static const uint8_t QUEUE_OTA_LEN    = 4;

/************************************************************
 *                VARIABLES GLOBALES
 ************************************************************/
Adafruit_NeoPixel rgb(NUM_PIXELS, RGB_PIN, NEO_GRB + NEO_KHZ800);

QueueHandle_t qEvents = nullptr;
QueueHandle_t qOta    = nullptr;

/************************************************************
 *                       SETUP
 ************************************************************/
void setup() {
  Serial.begin(115200);
  delay(1000);

  rgb_init();

  qEvents = xQueueCreate(QUEUE_EVENTS_LEN, sizeof(EcuEvent));
  qOta    = xQueueCreate(QUEUE_OTA_LEN, sizeof(OtaCmd));

  /* Sin bus CAN la ECU esta ciega: no hay nada util que hacer, asi que se
     queda en rojo fijo en vez de arrancar una maquina de estados que nunca
     va a recibir un dato. */
  if (!canInit()) {
    rgb_set(64, 0, 0);
    return;
  }

  xTaskCreatePinnedToCore(taskState, "state", STACK_STATE, nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(taskCan,   "can",   STACK_CAN,   nullptr, 4, nullptr, 1);
  xTaskCreatePinnedToCore(taskOta,   "ota",   STACK_OTA,   nullptr, 1, nullptr, 0);
}

/************************************************************
 *                        LOOP
 ************************************************************/
void loop() {
  /* Todo el trabajo vive en las tasks. Se cede el core para no comerse
     tiempo del scheduler al pedo. */
  vTaskDelay(pdMS_TO_TICKS(1000));
}
