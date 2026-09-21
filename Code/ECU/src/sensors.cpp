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

/* Ultima vez que se vio cada nodo, para detectar cuando uno se calla.
   Un valor 0 significa que nunca aparecio. Se indexa por posicion en
   NODE_IDENTIFIERS. */
static const uint32_t NODE_IDENTIFIERS[] = {
    CAN_ID_DRIVER,   CAN_ID_BMS,       CAN_ID_APPS, CAN_ID_BSE,
    CAN_ID_RPM_FRONT, CAN_ID_RPM_REAR, CAN_ID_RPM_MOTOR,
    CAN_ID_STEERING, CAN_ID_IMU,       CAN_ID_TPMS,
    CAN_ID_TEST_TEMPERATURE,
};
static const uint8_t NODE_COUNT = sizeof(NODE_IDENTIFIERS) / sizeof(NODE_IDENTIFIERS[0]);

static uint32_t lastSeenMilliseconds[NODE_COUNT] = {};

/* Estado de silencio de cada nodo y cuantas veces se callo. Los cuenta
   sensorsUpdateSilence(); el contador va al CSV y al monitor. */
static bool     isSilent[NODE_COUNT]  = {};
static uint16_t dropCount[NODE_COUNT] = {};

/* Identificadores que llegaron por el bus y no figuran en can_ids.h.
   Se guardan para mostrarlos en la pagina y poder bautizarlos. */
static uint32_t unknownIdentifiers[UNKNOWN_IDENTIFIER_MAXIMUM] = {};
static uint32_t unknownFrameCounts[UNKNOWN_IDENTIFIER_MAXIMUM] = {};
static uint8_t  unknownCount = 0;

static int8_t findNodeIndex(uint32_t identifier);

/**
 * @brief Marca en las flags que nodo emitio una trama.
 *
 * @param[in]  identifier  Identificador CAN de la trama que llego.
 *
 * @return void
 */
void sensorsMarkNodeSeen(uint32_t identifier) {
  int8_t index = findNodeIndex(identifier);
  if (index >= 0) {
    lastSeenMilliseconds[index] = millis();
  } else {
    sensorsNoteUnknownIdentifier(identifier);
  }

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

/**
 * @brief Posicion de un identificador en NODE_IDENTIFIERS.
 *
 * @param[in]  identifier  Identificador CAN.
 *
 * @return int8_t  Indice, o -1 si el identificador no es de un nodo.
 */
static int8_t findNodeIndex(uint32_t identifier) {
  for (uint8_t index = 0; index < NODE_COUNT; index++) {
    if (NODE_IDENTIFIERS[index] == identifier) {
      return index;
    }
  }
  return -1;
}

/**
 * @brief Revisa que nodos se callaron y cuenta cada caida.
 *
 * Solo se miran los nodos que se vieron alguna vez: los que nunca
 * estuvieron no cuentan como perdidos. Asi la ECU trabaja con los nodos
 * que existen en el banco y no con la lista completa del auto.
 *
 * @param[in]  silenceTimeoutMilliseconds  Cuanto silencio se tolera.
 *
 * @return uint32_t  Identificador CAN de un nodo que se acaba de callar
 *                   en esta llamada, o 0 si ninguno cayo ahora.
 */
uint32_t sensorsUpdateSilence(uint32_t silenceTimeoutMilliseconds) {
  uint32_t justDropped = 0;

  for (uint8_t index = 0; index < NODE_COUNT; index++) {
    bool everSeen = (lastSeenMilliseconds[index] != 0);
    bool silent   = everSeen &&
        (millis() - lastSeenMilliseconds[index] > silenceTimeoutMilliseconds);

    if (silent && !isSilent[index]) {
      dropCount[index]++;
      justDropped = NODE_IDENTIFIERS[index];
    }
    isSilent[index] = silent;
  }
  return justDropped;
}

/**
 * @brief Devuelve el primer nodo que esta callado en este momento.
 *
 * @return uint32_t  Identificador CAN, o 0 si todos emiten.
 */
uint32_t sensorsGetSilentNode(void) {
  for (uint8_t index = 0; index < NODE_COUNT; index++) {
    if (isSilent[index]) {
      return NODE_IDENTIFIERS[index];
    }
  }
  return 0;
}

/**
 * @brief Cuantas veces se callo un nodo desde que arranco la ECU.
 *
 * @param[in]  identifier  Identificador CAN del nodo.
 *
 * @return uint16_t  Cantidad de caidas.
 */
uint16_t sensorsGetDropCount(uint32_t identifier) {
  int8_t index = findNodeIndex(identifier);
  return (index < 0) ? 0 : dropCount[index];
}

/**
 * @brief Cantidad de nodos del mapa (can_ids.h) que la ECU vigila.
 *
 * @return uint8_t  Cantidad.
 */
uint8_t sensorsGetKnownNodeCount(void) {
  return NODE_COUNT;
}

/**
 * @brief Identificador CAN del nodo conocido en la posicion dada.
 *
 * @param[in]  index  De 0 a sensorsGetKnownNodeCount() - 1.
 *
 * @return uint32_t  Identificador, o 0 si la posicion no existe.
 */
uint32_t sensorsGetKnownNodeIdentifier(uint8_t index) {
  return (index < NODE_COUNT) ? NODE_IDENTIFIERS[index] : 0;
}

/**
 * @brief Indica si un nodo emitio alguna vez desde el encendido.
 *
 * @param[in]  identifier  Identificador CAN.
 *
 * @return bool  true si se lo vio al menos una vez.
 */
bool sensorsNodeWasEverSeen(uint32_t identifier) {
  int8_t index = findNodeIndex(identifier);
  return (index >= 0) && (lastSeenMilliseconds[index] != 0);
}

/**
 * @brief Indica si un nodo esta callado en este momento.
 *
 * @param[in]  identifier  Identificador CAN.
 *
 * @return bool  true si se lo vio y ahora no emite.
 */
bool sensorsNodeIsSilent(uint32_t identifier) {
  int8_t index = findNodeIndex(identifier);
  return (index >= 0) && isSilent[index];
}

/**
 * @brief Anota un identificador que no figura en el mapa can_ids.h.
 *
 * El heartbeat de la propia ECU tambien pasa por aca si se lo escucha
 * de vuelta; no se lo anota porque no es un nodo.
 *
 * @param[in]  identifier  Identificador CAN visto en el bus.
 *
 * @return void
 */
void sensorsNoteUnknownIdentifier(uint32_t identifier) {
  if (identifier == CAN_ID_ECU_STATE || identifier == CAN_ID_FAULT) {
    return;
  }

  for (uint8_t index = 0; index < unknownCount; index++) {
    if (unknownIdentifiers[index] == identifier) {
      unknownFrameCounts[index]++;
      return;
    }
  }

  if (unknownCount < UNKNOWN_IDENTIFIER_MAXIMUM) {
    unknownIdentifiers[unknownCount] = identifier;
    unknownFrameCounts[unknownCount] = 1;
    unknownCount++;
  }
}

/**
 * @brief Cuantos identificadores desconocidos se vieron.
 *
 * @return uint8_t  Cantidad.
 */
uint8_t sensorsGetUnknownIdentifierCount(void) {
  return unknownCount;
}

/**
 * @brief Identificador desconocido en la posicion dada.
 *
 * @param[in]  index  De 0 a sensorsGetUnknownIdentifierCount() - 1.
 *
 * @return uint32_t  Identificador, o 0 si la posicion no existe.
 */
uint32_t sensorsGetUnknownIdentifier(uint8_t index) {
  return (index < unknownCount) ? unknownIdentifiers[index] : 0;
}

/**
 * @brief Cuantas tramas llegaron de un identificador desconocido.
 *
 * @param[in]  index  De 0 a sensorsGetUnknownIdentifierCount() - 1.
 *
 * @return uint32_t  Cantidad de tramas, o 0 si la posicion no existe.
 */
uint32_t sensorsGetUnknownIdentifierFrameCount(uint8_t index) {
  return (index < unknownCount) ? unknownFrameCounts[index] : 0;
}
