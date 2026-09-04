/************************************************************
 *  Proyecto : ECU
 *  Archivo  : sensors.cpp
 *  Equipo   : UTN BA Motorsport Formula student team
 *  Fecha    : 2/9/2026
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Registro de que nodos estan presentes en el bus CAN.
 *
 *  Hardware:
 *  --------------------------------------------------------
 *  - MCU: ESP32 S3.
 *  - Sensores: cada uno vive en su propia placa y reporta por
 *    CAN. Esta ECU no lee ningun sensor de forma directa.
 *
 *  Notas:
 *  --------------------------------------------------------
 *  Un nodo se considera presente apenas emite una trama. No
 *  hace falta que la ECU le pregunte nada: si esta vivo, habla.
 *
 *  La conversion de cuentas a unidades de ingenieria no vive
 *  aca sino en el handler de cada trama, en can.cpp, que es
 *  donde estan los bytes crudos y el layout que los describe.
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
/* La escribe taskCan y la leen taskState y el monitoreo web. Cada bool
   se toca de a uno y solo pasa de false a true, asi que no necesita
   proteccion. */
static SensorFlags flags = {};

/**
 * @brief Marca en las flags que nodo emitio una trama.
 *
 * @param[in]  identifier  Identificador CAN de la trama que llego.
 *
 * @return void
 */
void sensorsMarkNodeSeen(uint32_t identifier) {
  switch (identifier) {
    case CAN_ID_DRIVER:           flags.driver            = true; break;
    case CAN_ID_BMS:              flags.batteryManagement = true; break;
    case CAN_ID_APPS:             flags.accelerator       = true; break;
    case CAN_ID_BSE:              flags.brake             = true; break;
    case CAN_ID_RPM_FRONT:        flags.wheelSpeedFront   = true; break;
    case CAN_ID_RPM_REAR:         flags.wheelSpeedRear    = true; break;
    case CAN_ID_RPM_MOTOR:        flags.motorSpeed        = true; break;
    case CAN_ID_STEERING:         flags.steeringWheel     = true; break;
    case CAN_ID_IMU:              flags.inertialUnit      = true; break;
    case CAN_ID_TPMS:             flags.tirePressure      = true; break;
    case CAN_ID_TEST_TEMPERATURE: flags.testTemperature   = true; break;

    default:
      break;
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
 * @brief Cuenta cuantos nodos distintos se detectaron en el bus.
 *
 * @return uint8_t  Cantidad de nodos presentes.
 */
uint8_t sensorsCountPresentNodes(void) {
  uint8_t count = 0;

  if (flags.driver)            count++;
  if (flags.batteryManagement) count++;
  if (flags.accelerator)       count++;
  if (flags.brake)             count++;
  if (flags.wheelSpeedFront)   count++;
  if (flags.wheelSpeedRear)    count++;
  if (flags.motorSpeed)        count++;
  if (flags.steeringWheel)     count++;
  if (flags.inertialUnit)      count++;
  if (flags.tirePressure)      count++;
  if (flags.testTemperature)   count++;

  return count;
}
