/************************************************************
 *  Proyecto : ECU
 *  Archivo  : errors.cpp
 *  Equipo   : UTN BA Motorsport Formula student team
 *  Fecha    : 2/9/2026
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Registro y latcheo de las fallas del vehiculo.
 *
 *  Hardware:
 *  --------------------------------------------------------
 *  - MCU: ESP32 S3.
 *  - Sensores: ninguno propio.
 *
 *  Notas:
 *  --------------------------------------------------------
 *  Una falla CRITICAL queda latcheada hasta el proximo reinicio.
 *  No existe una funcion para bajarla por software: si el auto
 *  entro en FAULT, alguien tiene que ir a mirar por que.
 *
 ************************************************************/

/************************************************************
 *                     INCLUDES
 ************************************************************/
#include "../include/ECU.h"
#include "../include/can_ids.h"

/************************************************************
 *                VARIABLES GLOBALES
 ************************************************************/
/* volatile porque las escribe taskCan y las lee taskState. Son de una
   sola palabra, asi que no hace falta un mutex. */
static volatile ErrorLevel currentLevel = ErrorLevel::NONE;
static volatile uint16_t   currentCode  = 0;
static volatile bool       latched      = false;

/**
 * @brief Registra una falla y, si es CRITICAL, la deja latcheada.
 *
 * La falla tambien se emite por CAN para que el resto de los nodos y la
 * telemetria se enteren sin tener que preguntar.
 *
 * @param[in]  level  Severidad de la falla.
 * @param[in]  code   Codigo propio del equipo para identificarla.
 *
 * @return void
 */
void errorReport(ErrorLevel level, uint16_t code) {
  /* Una WARNING posterior no debe tapar una CRITICAL ya registrada. */
  if (level >= currentLevel) {
    currentLevel = level;
    currentCode  = code;
  }

  if (level == ErrorLevel::CRITICAL) {
    latched = true;
  }

  /* Layout de la trama 0x080, definido en can_ids.h:
       byte 0    nivel de falla
       byte 1-2  codigo, big-endian */
  uint8_t payload[3];
  payload[0] = static_cast<uint8_t>(level);
  payload[1] = static_cast<uint8_t>(code >> 8);
  payload[2] = static_cast<uint8_t>(code & 0xFF);

  canSendFrame(CAN_ID_FAULT, payload, sizeof(payload));
}

/**
 * @brief Indica si hay una falla CRITICAL latcheada.
 *
 * @return bool  true si el vehiculo debe permanecer en FAULT.
 */
bool errorIsLatched(void) {
  return latched;
}

/**
 * @brief Devuelve el nivel de la ultima falla registrada.
 *
 * @return ErrorLevel  Nivel vigente.
 */
ErrorLevel errorGetLevel(void) {
  return currentLevel;
}

/**
 * @brief Devuelve el codigo de la ultima falla registrada.
 *
 * @return uint16_t  Codigo, o 0 si no hubo fallas.
 */
uint16_t errorGetCode(void) {
  return currentCode;
}

/**
 * @brief Devuelve el nombre imprimible de un nivel de falla.
 *
 * @param[in]  level  Nivel a nombrar.
 *
 * @return const char*  Nombre en mayusculas.
 */
const char *errorGetLevelName(ErrorLevel level) {
  switch (level) {
    case ErrorLevel::NONE:     return "NONE";
    case ErrorLevel::WARNING:  return "WARNING";
    case ErrorLevel::CRITICAL: return "CRITICAL";
  }
  return "DESCONOCIDO";
}
