/************************************************************
 *  Proyecto : ECU
 *  Archivo  : can.cpp
 *  Equipo   : UTN BA Motorsport Formula student team
 *  Fecha    : 2/9/2026
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Bus CAN del vehiculo sobre el periferico TWAI del ESP32:
 *  inicializacion, transmision, recepcion, y un handler por
 *  cada trama que la ECU sabe interpretar.
 *
 *  Hardware:
 *  --------------------------------------------------------
 *  - MCU: ESP32 S3.
 *  - Sensores: todos los nodos del auto cuelgan de este bus.
 *
 *  Notas:
 *  --------------------------------------------------------
 *  Bus a 500 kbps. Los identificadores y el layout de cada
 *  trama estan en can_ids.h; aca solo se aplican.
 *
 *  Los handlers convierten los bytes crudos a unidades de
 *  ingenieria y se los pasan al registrador. Esa conversion es
 *  el unico lugar del programa donde importa el layout, asi que
 *  si una trama cambia, se toca can_ids.h y este archivo.
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
static void handleTestTemperatureFrame(const twai_message_t *frame);
static void handleWheelSpeedFrontFrame(const twai_message_t *frame);
static void sendHeartbeatFrame(void);
static void recoverBusIfNeeded(void);
static uint16_t readUnsignedInteger16LittleEndian(const uint8_t *data);
static int16_t readSignedInteger16LittleEndian(const uint8_t *data);

/**
 * @brief Inicializa el periferico TWAI a 500 kbps en modo normal.
 *
 * Se acepta todo el trafico del bus porque la ECU necesita ver a todos
 * los nodos para saber cuales estan presentes, incluso los que todavia
 * no sabe interpretar.
 *
 * @return bool  true si el driver quedo instalado y arrancado.
 */
bool canInitialize(void) {
  twai_general_config_t generalConfiguration = TWAI_GENERAL_CONFIG_DEFAULT(
      (gpio_num_t)CAN_TRANSMIT_PIN, (gpio_num_t)CAN_RECEIVE_PIN,
      TWAI_MODE_NORMAL);
  twai_timing_config_t timingConfiguration = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t filterConfiguration = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&generalConfiguration, &timingConfiguration,
                          &filterConfiguration) != ESP_OK) {
    return false;
  }

  return (twai_start() == ESP_OK);
}

/**
 * @brief Transmite una trama estandar por el bus.
 *
 * @param[in]  identifier  Identificador CAN de 11 bits, ver can_ids.h.
 * @param[in]  data        Puntero a los bytes a enviar.
 * @param[in]  length      Cantidad de bytes, de 0 a 8.
 *
 * @return bool  true si la trama entro en la cola de transmision.
 */
bool canSendFrame(uint32_t identifier, const uint8_t *data, uint8_t length) {
  if (length > 8) {
    return false;
  }

  twai_message_t frame = {};
  frame.identifier       = identifier;
  frame.data_length_code = length;
  frame.extd             = 0; /* Todo el mapa usa identificadores de 11 bits. */

  for (uint8_t index = 0; index < length; index++) {
    frame.data[index] = data[index];
  }

  /* Sin espera: si la cola de transmision esta llena preferimos perder
     la trama antes que bloquear a la task que la mando. */
  return (twai_transmit(&frame, 0) == ESP_OK);
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
void canDispatchFrame(const twai_message_t *frame) {
  receivedFrameCount++;
  sensorsMarkNodeSeen(frame->identifier);

  switch (frame->identifier) {
    case CAN_ID_TEST_TEMPERATURE:
      handleTestTemperatureFrame(frame);
      break;

    case CAN_ID_RPM_FRONT:
      handleWheelSpeedFrontFrame(frame);
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
 * cableado esta mal. Ver recoverBusIfNeeded().
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
    twai_message_t frame;
    if (twai_receive(&frame, pdMS_TO_TICKS(RECEIVE_TIMEOUT_MILLISECONDS)) ==
        ESP_OK) {
      canDispatchFrame(&frame);
    }

    if (millis() - lastHeartbeatMilliseconds >= HEARTBEAT_PERIOD_MILLISECONDS) {
      lastHeartbeatMilliseconds = millis();
      sendHeartbeatFrame();
    }

    if (millis() - lastBusCheckMilliseconds >= BUS_CHECK_PERIOD_MILLISECONDS) {
      lastBusCheckMilliseconds = millis();
      recoverBusIfNeeded();
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
 * @brief Reengancha el controlador al bus si se cayo.
 *
 * En CAN toda trama transmitida tiene que ser confirmada por OTRO nodo.
 * Si la ECU esta sola en el bus, nadie le confirma el heartbeat: el
 * controlador reintenta, suma 8 a su contador de errores por cada
 * intento fallido, y al pasar de 255 se declara "bus off". Ahi se
 * desconecta solo y deja de transmitir Y de recibir.
 *
 * Lo importante es que de ese estado no sale por su cuenta. Sin esta
 * funcion, encender la ECU antes que los nodos la dejaria sorda para
 * siempre: aunque despues se enchufe un nodo que funciona, la ECU
 * seguiria contando cero tramas, que desde afuera se ve igual que un
 * cable mal puesto.
 *
 * La recuperacion tiene dos tiempos. Primero se pide, y el hardware
 * espera a que el bus este tranquilo. Cuando termina queda detenido, y
 * recien ahi se lo vuelve a arrancar; por eso el estado STOPPED se
 * trata aca como "termino de recuperarse".
 *
 * @return void
 */
static void recoverBusIfNeeded(void) {
  twai_status_info_t status;

  if (twai_get_status_info(&status) != ESP_OK) {
    return;
  }

  if (status.state == TWAI_STATE_BUS_OFF) {
    twai_initiate_recovery();
    busRecoveryCount++;
    Serial.printf("[%lu ms] CAN bus-off, reenganchando (van %lu)\n",
                  millis(), busRecoveryCount);
  } else if (status.state == TWAI_STATE_STOPPED) {
    /* La recuperacion termino. En este punto del programa STOPPED solo
       puede significar eso, porque canInitialize() ya arranco el bus
       antes de que existiera esta task. */
    twai_start();
  }
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
static void handleTestTemperatureFrame(const twai_message_t *frame) {
  /* Una trama corta significa que el emisor no respeta el layout: se
     descarta en vez de leer bytes que no existen. */
  if (frame->data_length_code < 4) {
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
 * Por ahora solo se registra la rueda izquierda, que es la unica que
 * tiene sensor. Cuando exista la derecha hay que agregarle su canal en
 * LogChannel y guardarla aca al lado.
 *
 * @param[in]  frame  Trama recibida.
 *
 * @return void
 */
static void handleWheelSpeedFrontFrame(const twai_message_t *frame) {
  if (frame->data_length_code < 4) {
    return;
  }

  uint16_t leftWheelRpm = readUnsignedInteger16LittleEndian(&frame->data[0]);

  loggerRecordValue(LogChannel::WHEEL_RPM_FRONT_LEFT, leftWheelRpm);
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
