/************************************************************
 *  Proyecto : ECU
 *  Archivo  : ECU.h
 *  Equipo   : UTN BA Motorsport Formula student team
 *  Fecha    : 23/12/2025
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
#define RGB_PIN    48
#define NUM_PIXELS 1

#define FAULT       0
#define BOOT        1
#define CONFIG      2
#define CAR_READY   3
#define CAR_ON      4

/************************************************************
 *             PROTOTIPOS DE FUNCIONES
 ************************************************************/

/**
 * @brief Inicializa el led RGB de la placa.
 *
 * @param[in]  void
 *
 * @return void           
 */
void rgb_init(void);

/**
 * @brief Setea el valor del led para configurar su color.
 *
 * @param[in]  r    valor de rojo
 * @param[in]  g    valor de verde
 * @param[in]  b    valor de azul
 *
 * @return void           
 */
void rgb_set(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Apagado del led RGB de la placa.
 *
 * @param[in]  void
 *
 * @return void           
 */
void rgb_off(void);