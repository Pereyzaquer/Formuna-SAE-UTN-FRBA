/************************************************************
 *  Proyecto : ECU
 *  Archivo  : indicators.cpp
 *  Equipo   : UTN BA Motorsport Formula student team
 *  Fecha    : 23/12/2025
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Led RGB de debug de la placa: indica el estado del
 *  vehiculo mientras se trabaja en el banco.
 *
 *  Hardware:
 *  --------------------------------------------------------
 *  - MCU: ESP32 S3.
 *  - Sensores:
 *
 *  Notas:
 *  --------------------------------------------------------
 *  Este led es solo para desarrollo. Los indicadores que exige
 *  el reglamento (RTD, TSAL, IMD) son otro circuito y todavia
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
extern Adafruit_NeoPixel rgb;

/************************************************************
 *               CONSTANTES DEL SISTEMA
 ************************************************************/
static const uint8_t BRIGHTNESS = 64; /**< Brillo fijo: a 255 encandila en banco. */

/**
 * @brief Inicializa el led RGB de la placa.
 *
 * @param[in]  void
 *
 * @return void
 */
void rgb_init() {
  rgb.begin();
  rgb.clear();
  rgb.show();
}

/**
 * @brief Setea el valor del led para configurar su color.
 *
 * @param[in]  r    valor de rojo
 * @param[in]  g    valor de verde
 * @param[in]  b    valor de azul
 *
 * @return void
 */
void rgb_set(uint8_t r, uint8_t g, uint8_t b) {
  rgb.setPixelColor(0, rgb.Color(r, g, b));
  rgb.show();
}

/**
 * @brief Apagado del led RGB de la placa.
 *
 * @param[in]  void
 *
 * @return void
 */
void rgb_off() {
  rgb.clear();
  rgb.show();
}

/**
 * @brief Refresca el led de debug segun el estado del vehiculo.
 *
 * El pulso de CAR_ON se calcula sobre millis() y no sobre un contador de
 * llamadas, asi el ritmo no cambia si se modifica el periodo de la task.
 *
 * @param[in]  state  Estado vigente.
 *
 * @return void
 */
void indicatorsUpdate(EcuState state) {
  switch (state) {
    case EcuState::BOOT:
      rgb_set(0, 0, BRIGHTNESS);            /* Azul     */
      break;

    case EcuState::CONFIG:
      rgb_set(BRIGHTNESS, BRIGHTNESS, 0);   /* Amarillo */
      break;

    case EcuState::CAR_READY:
      rgb_set(0, BRIGHTNESS, 0);            /* Verde fijo */
      break;

    case EcuState::CAR_ON: {
      /* Verde pulsante: triangular de 1 s para que se note a simple vista
         que el torque esta habilitado y no confundirlo con CAR_READY. */
      uint32_t phase = millis() % 1000;
      uint8_t  level = (phase < 500) ? (phase * BRIGHTNESS / 500)
                                     : ((1000 - phase) * BRIGHTNESS / 500);
      rgb_set(0, level, 0);
      break;
    }

    case EcuState::FAULT:
      rgb_set(BRIGHTNESS, 0, 0);            /* Rojo     */
      break;
  }
}
