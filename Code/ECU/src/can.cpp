/************************************************************
 *  Proyecto : ECU
 *  Archivo  : can.cpp
 *  Equipo   : UTN BA Motorsport Formula student team
 *  Fecha    : 10/9/2026
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Parte comun del bus CAN: la task que lo atiende, el
 *  heartbeat, y un handler por cada trama que la ECU sabe
 *  interpretar.
 *
 *  Hardware:
 *  --------------------------------------------------------
 *  - MCU: ESP32 S3.
 *  - Sensores: todos los nodos del auto cuelgan de este bus.
 *
 *  Notas:
 *  --------------------------------------------------------
 *  Este archivo no sabe con que chip se habla al bus. Eso lo
 *  resuelven can_twai.cpp (controlador interno del ESP32) o
 *  can_mcp2515.cpp (modulo externo por SPI), y platformio.ini
 *  elige cual se compila. Los dos exponen las mismas cuatro
 *  funciones canHardware*, declaradas en ECU.h.
 *
 *  Los identificadores y el layout de cada trama estan en
 *  can_ids.h; aca solo se aplican. Los handlers convierten los
 *  bytes crudos a unidades de ingenieria y se los pasan al
 *  registrador: esa conversion es el unico lugar del programa
 *  donde importa el layout.
 *
 ************************************************************/

/************************************************************
 *                     INCLUDES
 ************************************************************/
#include "../include/ECU.h"
#include "../include/can_ids.h"

/************************************************************
 *               CONSTANTES DEL SISTEMA
 ************************************************************/
static const uint32_t RECEIVE_TIMEOUT_MILLISECONDS = 10;
static const uint32_t HEARTBEAT_PERIOD_MILLISECONDS = 10; /**< 100 Hz. */

/** Cada cuanto se revisa si el controlador se cayo del bus. */
static const uint32_t BUS_CHECK_PERIOD_MILLISECONDS = 200;

/************************************************************
 *                VARIABLES GLOBALES
 ************************************************************/
/* Las incrementa taskCan y las lee el monitoreo web. */
static volatile uint32_t receivedFrameCount = 0;
static volatile uint32_t busRecoveryCount = 0;

/************************************************************
 *             PROTOTIPOS DE FUNCIONES LOCALES
 ************************************************************/
static void handleTestTemperatureFrame(const CanFrame *frame);
static void handleWheelSpeedFrontFrame(const CanFrame *frame);
static void handleWheelSpeedRearFrame(const CanFrame *frame);
static void sendHeartbeatFrame(void);
static void recordVehicleSpeed(void);
static uint16_t readUnsignedInteger16LittleEndian(const uint8_t *data);
static int16_t readSignedInteger16LittleEndian(const uint8_t *data);

/**
 * @brief Deja el bus CAN listo para usar.
 *
 * @return bool  true si el controlador quedo andando.
 */
bool canInitialize(void) {
  return canHardwareInitialize();
}

/**
 * @brief Transmite una trama estandar por el bus.
 *
 * @param[in]  identifier  Identificador CAN de 11 bits, ver can_ids.h.
 * @param[in]  data        Puntero a los bytes a enviar.
 * @param[in]  length      Cantidad de bytes, de 0 a 8.
 *
 * @return bool  true si el controlador acepto la trama.
 */
bool canSendFrame(uint32_t identifier, const uint8_t *data, uint8_t length) {
  if (length > 8) {
    return false;
  }

  CanFrame frame;
  frame.identifier = identifier;
  frame.length     = length;

  for (uint8_t index = 0; index < length; index++) {
    frame.data[index] = data[index];
  }

  return canHardwareSend(&frame);
}

/**
 * @brief Despacha una trama recibida al handler de su identificador.
 *
 * Toda trama recibida cuenta como señal de vida del nodo que la emitio,
 * por eso la presencia se marca aca y no dentro de cada handler. Los
 * identificadores que todavia no tienen handler igual quedan contados
 * como nodo presente, que es lo que necesita el arranque.
 *
 * @param[in]  frame  Trama recibida.
 *
 * @return void
 */
void canDispatchFrame(const CanFrame *frame) {
  receivedFrameCount++;
  sensorsMarkNodeSeen(frame->identifier);

  switch (frame->identifier) {
    case CAN_ID_TEST_TEMPERATURE:
      handleTestTemperatureFrame(frame);
      break;

    case CAN_ID_RPM_FRONT:
      handleWheelSpeedFrontFrame(frame);
      break;

    case CAN_ID_RPM_REAR:
      handleWheelSpeedRearFrame(frame);
      break;

    default:
      /* Nodo conocido sin handler todavia, o identificador ajeno al
         mapa. En los dos casos ya quedo registrada su presencia. */
      break;
  }
}

/**
 * @brief Devuelve cuantas tramas se recibieron desde el arranque.
 *
 * @return uint32_t  Cantidad de tramas recibidas.
 */
uint32_t canGetReceivedFrameCount(void) {
  return receivedFrameCount;
}

/**
 * @brief Cuantas veces hubo que reenganchar el controlador al bus.
 *
 * Si este numero crece sin parar, la ECU esta sola en el bus o el
 * cableado esta mal: nadie le esta confirmando lo que transmite.
 *
 * @return uint32_t  Cantidad de recuperaciones.
 */
uint32_t canGetBusRecoveryCount(void) {
  return busRecoveryCount;
}

/**
 * @brief Task de servicio del bus CAN: recibe, despacha y transmite.
 *
 * @param[in]  argument  No se usa. Lo exige la firma de FreeRTOS.
 *
 * @return void
 */
void taskCan(void *argument) {
  (void)argument;

  uint32_t lastHeartbeatMilliseconds = 0;
  uint32_t lastBusCheckMilliseconds = 0;

  for (;;) {
    CanFrame frame;
    if (canHardwareReceive(&frame, RECEIVE_TIMEOUT_MILLISECONDS)) {
      canDispatchFrame(&frame);
    }

    if (millis() - lastHeartbeatMilliseconds >= HEARTBEAT_PERIOD_MILLISECONDS) {
      lastHeartbeatMilliseconds = millis();
      sendHeartbeatFrame();
    }

    /* En CAN toda trama transmitida tiene que ser confirmada por OTRO
       nodo. Si la ECU esta sola en el bus, nadie le confirma el
       heartbeat, el controlador acumula errores y termina desconectado
       del bus. Sin este chequeo, encender la ECU antes que los nodos la
       dejaria sorda para siempre, que desde afuera se ve igual que un
       cable mal puesto. */
    if (millis() - lastBusCheckMilliseconds >= BUS_CHECK_PERIOD_MILLISECONDS) {
      lastBusCheckMilliseconds = millis();
      if (canHardwareRecoverIfNeeded()) {
        busRecoveryCount++;
        Serial.printf("[%lu ms] CAN caido, reenganchando (van %lu)\n",
                      millis(), busRecoveryCount);
      }
    }
  }
}

/************************************************************
 *                  FUNCIONES LOCALES
 ************************************************************/

/**
 * @brief Emite el estado de la ECU y el heartbeat a 100 Hz.
 *
 * Es el latido con el que el resto de los nodos sabe que la ECU
 * principal sigue viva.
 *
 * @return void
 */
static void sendHeartbeatFrame(void) {
  static uint8_t heartbeatCounter = 0;

  uint8_t payload[2];
  payload[0] = static_cast<uint8_t>(stateGet());
  payload[1] = heartbeatCounter;
  heartbeatCounter++;

  canSendFrame(CAN_ID_ECU_STATE, payload, sizeof(payload));
}

/**
 * @brief Interpreta la trama del nodo de temperatura DHT11.
 *
 * Layout, definido en can_ids.h:
 *   byte 0-1  temperatura, int16,  little-endian, 0.1 grados C/bit
 *   byte 2-3  humedad,     uint16, little-endian, 0.1 % HR/bit
 *
 * @param[in]  frame  Trama recibida.
 *
 * @return void
 */
static void handleTestTemperatureFrame(const CanFrame *frame) {
  /* Una trama corta significa que el emisor no respeta el layout: se
     descarta en vez de leer bytes que no existen. */
  if (frame->length < 4) {
    return;
  }

  int16_t  temperatureRaw = readSignedInteger16LittleEndian(&frame->data[0]);
  uint16_t humidityRaw    = readUnsignedInteger16LittleEndian(&frame->data[2]);

  /* La escala es 0.1 por bit, de ahi la division por diez. */
  loggerRecordValue(LogChannel::TEMPERATURE_CELSIUS, temperatureRaw / 10.0f);
  loggerRecordValue(LogChannel::HUMIDITY_PERCENT, humidityRaw / 10.0f);
}

/**
 * @brief Interpreta la trama de RPM de las ruedas delanteras.
 *
 * Layout, definido en can_ids.h:
 *   byte 0-1  RPM rueda izquierda, uint16, little-endian, 1 RPM/bit
 *   byte 2-3  RPM rueda derecha,   uint16, little-endian, 1 RPM/bit
 *
 * @param[in]  frame  Trama recibida.
 *
 * @return void
 */
static void handleWheelSpeedFrontFrame(const CanFrame *frame) {
  if (frame->length < 4) {
    return;
  }

  uint16_t leftRpm  = readUnsignedInteger16LittleEndian(&frame->data[0]);
  uint16_t rightRpm = readUnsignedInteger16LittleEndian(&frame->data[2]);

  /* Un nodo con un solo sensor marca la otra rueda como no medida. Esa
     rueda no se registra ni entra en la velocidad del auto. */
  if (leftRpm != RPM_NOT_MEASURED) {
    speedUpdateWheel(WheelPosition::FRONT_LEFT, leftRpm);
    loggerRecordValue(LogChannel::WHEEL_RPM_FRONT_LEFT, leftRpm);
  }
  if (rightRpm != RPM_NOT_MEASURED) {
    speedUpdateWheel(WheelPosition::FRONT_RIGHT, rightRpm);
    loggerRecordValue(LogChannel::WHEEL_RPM_FRONT_RIGHT, rightRpm);
  }

  recordVehicleSpeed();
}

/**
 * @brief Interpreta la trama de RPM de las ruedas traseras.
 *
 * Mismo layout que la delantera.
 *
 * @param[in]  frame  Trama recibida.
 *
 * @return void
 */
static void handleWheelSpeedRearFrame(const CanFrame *frame) {
  if (frame->length < 4) {
    return;
  }

  uint16_t leftRpm  = readUnsignedInteger16LittleEndian(&frame->data[0]);
  uint16_t rightRpm = readUnsignedInteger16LittleEndian(&frame->data[2]);

  /* Un nodo con un solo sensor marca la otra rueda como no medida. Esa
     rueda no se registra ni entra en la velocidad del auto. */
  if (leftRpm != RPM_NOT_MEASURED) {
    speedUpdateWheel(WheelPosition::REAR_LEFT, leftRpm);
    loggerRecordValue(LogChannel::WHEEL_RPM_REAR_LEFT, leftRpm);
  }
  if (rightRpm != RPM_NOT_MEASURED) {
    speedUpdateWheel(WheelPosition::REAR_RIGHT, rightRpm);
    loggerRecordValue(LogChannel::WHEEL_RPM_REAR_RIGHT, rightRpm);
  }

  recordVehicleSpeed();
}

/**
 * @brief Recalcula la velocidad del vehiculo y la guarda.
 *
 * Se llama despues de cada trama de ruedas y no en una task aparte,
 * porque la velocidad solo puede cambiar cuando llega un dato nuevo:
 * recalcularla a intervalo fijo repetiria el mismo numero.
 *
 * @return void
 */
static void recordVehicleSpeed(void) {
  float kilometersPerHour;

  if (speedGetVehicleKph(&kilometersPerHour)) {
    loggerRecordValue(LogChannel::VEHICLE_SPEED_KPH, kilometersPerHour);
  }
}

/**
 * @brief Arma un entero sin signo de 16 bits guardado little-endian.
 *
 * Little-endian quiere decir que el byte menos significativo va
 * primero: los bytes 0x2C 0x01 forman el numero 0x012C, o sea 300.
 *
 * @param[in]  data  Puntero a los dos bytes.
 *
 * @return uint16_t  Numero armado.
 */
static uint16_t readUnsignedInteger16LittleEndian(const uint8_t *data) {
  return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

/**
 * @brief Arma un entero con signo de 16 bits guardado little-endian.
 *
 * @param[in]  data  Puntero a los dos bytes.
 *
 * @return int16_t  Numero armado, negativo incluido.
 */
static int16_t readSignedInteger16LittleEndian(const uint8_t *data) {
  return (int16_t)readUnsignedInteger16LittleEndian(data);
}
