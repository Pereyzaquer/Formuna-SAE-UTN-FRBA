/************************************************************
 *  Proyecto : ECU
 *  Archivo  : sensors.cpp
 *  Equipo   : UTN BA Motorsport Formula student team
 *  Fecha    : 14/8/2026
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Presencia, configuracion y escalado de los sensores del
 *  vehiculo.
 *
 *  Hardware:
 *  --------------------------------------------------------
 *  - MCU: ESP32 S3.
 *  - Sensores: cada uno vive en su propia placa y reporta a
 *    esta ECU por CAN. Aca no se lee ningun ADC local.
 *
 *  Notas:
 *  --------------------------------------------------------
 *  Todo este archivo es esqueleto: el set final de sensores no
 *  esta cerrado y no hay curvas de calibracion todavia.
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
/* Escrita por taskCan, leida por taskState. Los bool se actualizan de a
   uno y solo pasan de false a true, asi que no hace falta proteccion. */
static SensorFlags flags = {};

/**
 * @brief Marca en las flags que nodo reporto presencia en el bus.
 *
 * Se llama desde el dispatcher con cada trama recibida: recibir algo de
 * un ID equivale a que ese nodo esta vivo.
 *
 * @param[in]  canId  ID CAN del mensaje que llego.
 *
 * @return void
 */
void sensorsMarkSeen(uint32_t canId) {
  switch (canId) {
    case CAN_ID_DRIVER:    flags.driver   = true; break;
    case CAN_ID_BMS:       flags.bms      = true; break;
    case CAN_ID_APPS_BSE:  flags.apps     = true;
                           flags.bse      = true; break;  /* Van en la misma trama. */
    case CAN_ID_RPM_FRONT: flags.rpmFront = true; break;
    case CAN_ID_RPM_REAR:  flags.rpmRear  = true; break;
    case CAN_ID_STEERING:  flags.steering = true; break;
    case CAN_ID_IMU:       flags.imu      = true; break;
    case CAN_ID_TPMS:      flags.tpms     = true; break;
    default:                                      break;
  }
}

/**
 * @brief Devuelve las flags de presencia acumuladas.
 *
 * @return SensorFlags  Copia de las flags vigentes.
 */
SensorFlags sensorsGetFlags(void) {
  return flags;
}

/**
 * @brief Envia la configuracion inicial a cada nodo presente.
 *
 * Los nodos ausentes se saltean sin bloquear el arranque: los de
 * categoria A porque el reglamento no los exige para correr, y los de
 * categoria B porque su ausencia ya mando el auto a FAULT antes de
 * llegar a CONFIG.
 *
 * @return void
 */
void sensorsConfigure(void) {
  /* TODO: no existe todavia el mensaje de configuracion. Falta definir en
     can_ids.h si se configura por un ID propio de configuracion o por un
     campo dentro de la trama de cada nodo, y que parametros se mandan
     (periodo de reporte, rango, filtros). Hasta entonces esto no hace nada. */

  if (flags.apps) {
    /* TODO: enviar configuracion del APPS (rangos de calibracion). */
  }
  if (flags.bse) {
    /* TODO: enviar configuracion del sensor de freno. */
  }
  if (flags.rpmFront || flags.rpmRear) {
    /* TODO: enviar cantidad de dientes / imanes de la rueda fonica. */
  }
  if (flags.steering) {
    /* TODO: enviar el cero del volante. */
  }
  if (flags.imu) {
    /* TODO: enviar rangos de acelerometro y giroscopo. */
  }
  if (flags.tpms) {
    /* TODO: enviar presiones objetivo por rueda. */
  }
}

/**
 * @brief Escala la lectura cruda del acelerador a recorrido de pedal.
 *
 * @param[in]  raw  Valor crudo recibido por CAN.
 *
 * @return float  Recorrido de pedal en porcentaje, 0.0 a 100.0.
 */
float sensorsScaleApps(uint16_t raw) {
  (void)raw;
  /* TODO: falta la calibracion. Hay que medir en el auto el valor crudo con
     el pedal en reposo y a fondo, y decidir si la curva es lineal. Devolver
     0 mientras tanto mantiene el torque en ralenti, que es lo seguro. */
  return 0.0f;
}

/**
 * @brief Escala la lectura cruda del freno a presion de linea.
 *
 * @param[in]  raw  Valor crudo recibido por CAN.
 *
 * @return float  Presion en bar.
 */
float sensorsScaleBse(uint16_t raw) {
  (void)raw;
  /* TODO: falta la hoja de datos del sensor de presion elegido para armar
     la conversion cuentas -> bar. */
  return 0.0f;
}
