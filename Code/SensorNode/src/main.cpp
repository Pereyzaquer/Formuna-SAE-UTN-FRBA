/************************************************************
 *  Proyecto : SensorNode
 *  Archivo  : main.cpp
 *  Equipo   : UTN BA Motorsport Formula student team
 *  Fecha    : 2/9/2026
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Nodo sensor del bus CAN. Mide con el sensor que tenga
 *  conectado y publica el resultado en su identificador, al
 *  ritmo que ese sensor permita.
 *
 *  Hardware:
 *  --------------------------------------------------------
 *  - MCU: ESP32 C3.
 *  - Sensores: uno por nodo, elegido con el entorno de
 *    platformio.ini. Ver sensor_temperature.cpp y
 *    sensor_wheel_speed.cpp.
 *
 *  Notas:
 *  --------------------------------------------------------
 *  Este archivo no sabe que sensor tiene conectado y no deberia
 *  saberlo nunca: le pregunta a la interfaz de SensorNode.h su
 *  identificador, su periodo y su trama.
 *
 *  El nodo no espera ninguna orden de la ECU: arranca midiendo
 *  y emitiendo. Es lo que hace que la ECU pueda descubrirlo con
 *  solo escuchar el bus, y que un nodo ausente no trabe nada.
 *
 ************************************************************/

/************************************************************
 *                     INCLUDES
 ************************************************************/
#include "../include/SensorNode.h"

/************************************************************
 *                VARIABLES GLOBALES
 ************************************************************/
static uint32_t lastTransmissionMilliseconds = 0;

/************************************************************
 *                       SETUP
 ************************************************************/
void setup() {
  Serial.begin(115200);
  delay(1000);

  sensorInitialize();

  if (!canInitialize()) {
    Serial.println("Error: no se pudo inicializar el bus CAN");
    return;
  }

  Serial.print("Nodo listo. Identificador CAN: 0x");
  Serial.println(sensorGetCanIdentifier(), HEX);
  Serial.print("Periodo de emision: ");
  Serial.print(sensorGetPeriodMilliseconds());
  Serial.println(" ms");
}

/************************************************************
 *                        LOOP
 ************************************************************/
void loop() {
  if (millis() - lastTransmissionMilliseconds <
      sensorGetPeriodMilliseconds()) {
    return;
  }
  lastTransmissionMilliseconds = millis();

  uint8_t data[8];
  uint8_t length = 0;

  /* Si la medicion fallo no se emite nada. Es preferible un hueco en el
     grafico antes que publicar un numero inventado. */
  if (!sensorBuildFrame(data, &length)) {
    return;
  }

  canSendFrame(sensorGetCanIdentifier(), data, length);
}
