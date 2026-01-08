/************************************************************
 *  Proyecto : RPM_Sensor
 *  Archivo  : funtions.cpp
 *  Equipo   : UTN BA Motorsport Formula student team 
 *  Fecha    : 8/1/2026
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Funciones de adquisicion de datos, configuracion de
 *  sensores, etc.
 *
 *  Hardware:
 *  --------------------------------------------------------
 *  - MCU: ESP32 mini.
 *  - Sensores: 
 *
 *  Notas:
 *  --------------------------------------------------------
 *
 ************************************************************/

/************************************************************
 *                     INCLUDES
 ************************************************************/
#include "../include/RPM.h"

/************************************************************
 *                VARIABLES GLOBALES EXTERNAS
 ************************************************************/

//FUNCIONES QUE DEJE DEL PROGRAMA DE PRUEBA PARA PROBAR LOS ESTADOS CON EL LED RGB QUE TIENE LA PLACA (LA ESP32 S3, NO LA ESP8266)

/**
 * @brief Inicializa el led RGB de la placa.
 *
 * @param[in]  void
 *
 * @return void           
 */
void rgb_init() {
  
}