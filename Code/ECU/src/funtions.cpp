/************************************************************
 *  Proyecto : ECU
 *  Archivo  : funtions.cpp
 *  Equipo   : UTN BA Motorsport Formula student team 
 *  Fecha    : 23/12/2025
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Funciones de adquisicion de datos, configuracion de
 *  sensores, manejo del estado del vehiculo, etc.
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
 *                VARIABLES GLOBALES EXTERNAS
 ************************************************************/
extern uint32_t ESTADO;
extern Adafruit_NeoPixel rgb;

//FUNCIONES QUE DEJE DEL PROGRAMA DE PRUEBA PARA PROBAR LOS ESTADOS CON EL LED RGB QUE TIENE LA PLACA (LA ESP32 S3, NO LA ESP8266)

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