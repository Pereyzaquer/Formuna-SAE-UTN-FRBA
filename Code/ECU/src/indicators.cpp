/************************************************************
 *  Proyecto : ECU
 *  Archivo  : indicators.cpp
 *  Equipo   : UTN BA Motorsport Formula student team
 *  Fecha    : 2/9/2026
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Led RGB de debug de la placa: muestra el estado del
 *  vehiculo mientras se trabaja en el banco.
 *
 *  Hardware:
 *  --------------------------------------------------------
 *  - MCU: ESP32 S3.
 *  - Sensores: led RGB incluido en la placa.
 *
 *  Notas:
 *  --------------------------------------------------------
 *  Este led es solo para desarrollo. Los indicadores que exige
 *  el reglamento, RTD, TSAL e IMD, son otro circuito y todavia
 *  no estan implementados.
 *
 ************************************************************/

/************************************************************
 *                     INCLUDES
 ************************************************************/
#include "../include/ECU.h"

/************************************************************
 *                VARIABLES GLOBALES EXTERNAS
 ************************************************************/
extern Adafruit_NeoPixel rgbLed;

/************************************************************
 *               CONSTANTES DEL SISTEMA
 ************************************************************/
/** Brillo fijo. A 255 el led encandila cuando se trabaja de cerca. */
static const uint8_t BRIGHTNESS = 64;

/** Duracion de un ciclo completo del pulso de CAR_ON. */
static const uint32_t PULSE_PERIOD_MILLISECONDS = 1000;

/**
 * @brief Inicializa el led RGB de la placa.
 *
 * @return void
 */
void indicatorsInitialize(void) {
  rgbLed.begin();
  rgbLed.clear();
  rgbLed.show();
}

/**
 * @brief Enciende el led RGB con un color.
 *
 * @param[in]  red    Componente rojo, 0 a 255.
 * @param[in]  green  Componente verde, 0 a 255.
 * @param[in]  blue   Componente azul, 0 a 255.
 *
 * @return void
 */
void indicatorsSetColor(uint8_t red, uint8_t green, uint8_t blue) {
  rgbLed.setPixelColor(0, rgbLed.Color(red, green, blue));
  rgbLed.show();
}

/**
 * @brief Refresca el led de debug segun el estado del vehiculo.
 *
 * El pulso de CAR_ON se calcula sobre millis() y no sobre un contador
 * de llamadas, asi el ritmo no cambia si se modifica cada cuanto se
 * llama a esta funcion.
 *
 * @param[in]  state  Estado vigente.
 *
 * @return void
 */
void indicatorsUpdate(EcuState state) {
  switch (state) {
    case EcuState::BOOT:
      indicatorsSetColor(0, 0, BRIGHTNESS);           /* Azul     */
      break;

    case EcuState::CONFIG:
      indicatorsSetColor(BRIGHTNESS, BRIGHTNESS, 0);  /* Amarillo */
      break;

    case EcuState::CAR_READY:
      indicatorsSetColor(0, BRIGHTNESS, 0);           /* Verde fijo */
      break;

    case EcuState::CAR_ON: {
      /* Verde pulsante, para distinguir a simple vista que el torque
         esta habilitado y no confundirlo con CAR_READY. El brillo sube
         y baja en forma de triangulo a lo largo del periodo. */
      uint32_t phase = millis() % PULSE_PERIOD_MILLISECONDS;
      uint32_t halfPeriod = PULSE_PERIOD_MILLISECONDS / 2;

      uint8_t level = (phase < halfPeriod)
                          ? (phase * BRIGHTNESS / halfPeriod)
                          : ((PULSE_PERIOD_MILLISECONDS - phase) * BRIGHTNESS /
                             halfPeriod);

      indicatorsSetColor(0, level, 0);
      break;
    }

    case EcuState::FAULT:
      indicatorsSetColor(BRIGHTNESS, 0, 0);           /* Rojo     */
      break;
  }
}
