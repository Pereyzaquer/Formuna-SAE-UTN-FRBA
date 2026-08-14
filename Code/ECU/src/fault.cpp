/************************************************************
 *  Proyecto : ECU
 *  Archivo  : fault.cpp
 *  Equipo   : UTN BA Motorsport Formula student team
 *  Fecha    : 14/8/2026
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Registro y latcheo de fallas del vehiculo.
 *
 *  Hardware:
 *  --------------------------------------------------------
 *  - MCU: ESP32 S3.
 *  - Sensores:
 *
 *  Notas:
 *  --------------------------------------------------------
 *  Una falla CRITICAL queda latcheada hasta el proximo reset.
 *  No existe una funcion para bajarla por software: si el auto
 *  entro en FAULT alguien tiene que ir a mirar por que.
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
/* volatile porque las escribe la task de CAN y las lee la de estados.
   Son de una sola palabra, asi que no hace falta mutex. */
static volatile FaultLevel currentLevel = FaultLevel::NONE;
static volatile uint16_t   currentCode  = 0;
static volatile bool       latched      = false;

/**
 * @brief Registra una falla y, si es CRITICAL, la latchea.
 *
 * La falla se emite tambien por CAN para que el resto de los nodos y la
 * telemetria se enteren sin tener que preguntar.
 *
 * @param[in]  level  Severidad de la falla.
 * @param[in]  code   Codigo propio del equipo para identificarla.
 *
 * @return void
 */
void faultReport(FaultLevel level, uint16_t code) {
  /* Una WARNING posterior no debe tapar una CRITICAL ya registrada. */
  if (level >= currentLevel) {
    currentLevel = level;
    currentCode  = code;
  }

  if (level == FaultLevel::CRITICAL) {
    latched = true;
  }

  /* TODO: cerrar el layout de la trama 0x080 en can_ids.h. Por ahora se
     manda nivel + codigo en crudo, que es lo minimo util para debug. */
  uint8_t payload[3] = {
    static_cast<uint8_t>(level),
    static_cast<uint8_t>(code >> 8),
    static_cast<uint8_t>(code & 0xFF)
  };
  canSend(CAN_ID_FAULT, payload, sizeof(payload));
}

/**
 * @brief Indica si hay una falla CRITICAL latcheada.
 *
 * @return bool  true si el vehiculo debe permanecer en FAULT.
 */
bool faultIsLatched(void) {
  return latched;
}

/**
 * @brief Devuelve el nivel de la ultima falla registrada.
 *
 * @return FaultLevel  Nivel vigente.
 */
FaultLevel faultGetLevel(void) {
  return currentLevel;
}

/**
 * @brief Devuelve el codigo de la ultima falla registrada.
 *
 * @return uint16_t  Codigo, o 0 si no hubo fallas.
 */
uint16_t faultGetCode(void) {
  return currentCode;
}

/**
 * @brief Devuelve el nombre imprimible de un nivel de falla.
 *
 * @param[in]  level  Nivel a nombrar.
 *
 * @return const char*  Nombre en mayusculas.
 */
const char *faultLevelName(FaultLevel level) {
  switch (level) {
    case FaultLevel::NONE:     return "NONE";
    case FaultLevel::WARNING:  return "WARNING";
    case FaultLevel::CRITICAL: return "CRITICAL";
  }
  return "?";
}
