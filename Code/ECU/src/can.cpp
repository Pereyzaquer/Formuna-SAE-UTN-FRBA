/************************************************************
 *  Proyecto : ECU
 *  Archivo  : can.cpp
 *  Equipo   : UTN BA Motorsport Formula student team
 *  Fecha    : 14/8/2026
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Bus CAN del vehiculo sobre el periferico TWAI del ESP32:
 *  inicializacion, transmision, recepcion y despacho de las
 *  tramas a su handler.
 *
 *  Hardware:
 *  --------------------------------------------------------
 *  - MCU: ESP32 S3.
 *  - Sensores: todos los nodos del auto cuelgan de este bus.
 *
 *  Notas:
 *  --------------------------------------------------------
 *  Bus a 500 kbps. Los IDs viven en can_ids.h; los handlers de
 *  este archivo todavia no parsean nada, solo marcan presencia.
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
static const uint32_t CAN_RX_TIMEOUT_MS = 10;   /**< Espera por trama en taskCan. */
static const uint32_t HEARTBEAT_MS      = 10;   /**< 100 Hz de CAN_ID_ECU_STATE.  */

/************************************************************
 *                VARIABLES GLOBALES
 ************************************************************/
/* La incrementa taskCan y la lee el monitoreo web. */
static volatile uint32_t rxCount = 0;

/************************************************************
 *             PROTOTIPOS DE FUNCIONES LOCALES
 ************************************************************/
static void handleDriver(const twai_message_t *msg);
static void handleBms(const twai_message_t *msg);
static void handleAppsBse(const twai_message_t *msg);
static void handleRpm(const twai_message_t *msg);
static void handleSteering(const twai_message_t *msg);
static void handleImu(const twai_message_t *msg);
static void handleTpms(const twai_message_t *msg);
static void handleFault(const twai_message_t *msg);
static void sendHeartbeat(void);

/**
 * @brief Inicializa el periferico TWAI a 500 kbps en modo normal.
 *
 * Se acepta todo el trafico (filtro pasa-todo) porque la ECU principal
 * necesita ver a todos los nodos para detectar cuales estan presentes.
 *
 * @return bool  true si el driver quedo instalado y arrancado.
 */
bool canInit(void) {
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
      (gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, TWAI_MODE_NORMAL);
  twai_timing_config_t  t = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t  f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&g, &t, &f) != ESP_OK) {
    return false;
  }
  return (twai_start() == ESP_OK);
}

/**
 * @brief Transmite una trama estandar por el bus.
 *
 * @param[in]  id    Identificador CAN de 11 bits (ver can_ids.h).
 * @param[in]  data  Puntero a los bytes a enviar.
 * @param[in]  len   Cantidad de bytes, 0 a 8.
 *
 * @return bool  true si la trama entro en la cola de TX del driver.
 */
bool canSend(uint32_t id, const uint8_t *data, uint8_t len) {
  if (len > 8) {
    return false;
  }

  twai_message_t msg = {};
  msg.identifier       = id;
  msg.data_length_code = len;
  msg.extd             = 0;   /* Todo el mapa del auto usa IDs de 11 bits. */

  for (uint8_t i = 0; i < len; i++) {
    msg.data[i] = data[i];
  }

  /* Sin espera: si la cola de TX esta llena preferimos perder la trama
     antes que bloquear la task que la mando. */
  return (twai_transmit(&msg, 0) == ESP_OK);
}

/**
 * @brief Recibe una trama del bus.
 *
 * @param[out] msg          Trama recibida.
 * @param[in]  timeoutMs    Tiempo maximo de espera en milisegundos.
 *
 * @return bool  true si se recibio una trama.
 */
bool canReceive(twai_message_t *msg, uint32_t timeoutMs) {
  return (twai_receive(msg, pdMS_TO_TICKS(timeoutMs)) == ESP_OK);
}

/**
 * @brief Despacha una trama recibida al handler que corresponde a su ID.
 *
 * Toda trama recibida cuenta como señal de vida del nodo que la emitio,
 * por eso se marca la presencia aca y no dentro de cada handler.
 *
 * @param[in]  msg  Trama recibida.
 *
 * @return void
 */
void canDispatch(const twai_message_t *msg) {
  rxCount++;
  sensorsMarkSeen(msg->identifier);

  switch (msg->identifier) {
    case CAN_ID_FAULT:     handleFault(msg);    break;
    case CAN_ID_APPS_BSE:  handleAppsBse(msg);  break;
    case CAN_ID_RPM_FRONT: handleRpm(msg);      break;
    case CAN_ID_RPM_REAR:  handleRpm(msg);      break;
    case CAN_ID_STEERING:  handleSteering(msg); break;
    case CAN_ID_DRIVER:    handleDriver(msg);   break;
    case CAN_ID_BMS:       handleBms(msg);      break;
    case CAN_ID_IMU:       handleImu(msg);      break;
    case CAN_ID_TPMS:      handleTpms(msg);     break;
    default:                                    break;  /* ID ajeno al mapa: se ignora. */
  }
}

/**
 * @brief Devuelve cuantas tramas se recibieron desde el arranque.
 *
 * @return uint32_t  Cantidad de tramas recibidas.
 */
uint32_t canGetRxCount(void) {
  return rxCount;
}

/**
 * @brief Task de servicio del bus CAN: recibe, despacha y transmite.
 *
 * @param[in]  arg  No se usa. Requerido por la firma de FreeRTOS.
 *
 * @return void
 */
void taskCan(void *arg) {
  (void)arg;

  uint32_t lastHeartbeat = 0;

  for (;;) {
    twai_message_t msg;
    if (canReceive(&msg, CAN_RX_TIMEOUT_MS)) {
      canDispatch(&msg);
    }

    if (millis() - lastHeartbeat >= HEARTBEAT_MS) {
      lastHeartbeat = millis();
      sendHeartbeat();
    }
  }
}

/************************************************************
 *                  FUNCIONES LOCALES
 ************************************************************/

/**
 * @brief Emite el estado de la ECU y el heartbeat a 100 Hz.
 *
 * Es el latido que el resto de los nodos usa para saber que la ECU
 * principal sigue viva.
 *
 * @return void
 */
static void sendHeartbeat(void) {
  static uint8_t counter = 0;

  /* TODO: cerrar el layout de la trama 0x100 en can_ids.h. Falta definir
     como se codifican las flags de sensores en el resto de los bytes. */
  uint8_t payload[2] = { static_cast<uint8_t>(stateGet()), counter++ };
  canSend(CAN_ID_ECU_STATE, payload, sizeof(payload));
}

/**
 * @brief Handler de la trama de falla / emergencia (0x080).
 *
 * @param[in]  msg  Trama recibida.
 *
 * @return void
 */
static void handleFault(const twai_message_t *msg) {
  (void)msg;
  /* TODO: parsear nivel y codigo segun el layout de 0x080 y llamar a
     faultReport() con lo que reporto el nodo remoto. */
}

/**
 * @brief Handler de la trama del driver / inversor (0x300).
 *
 * @param[in]  msg  Trama recibida.
 *
 * @return void
 */
static void handleDriver(const twai_message_t *msg) {
  (void)msg;
  /* TODO: parsear estado del inversor segun la hoja de datos del fabricante.
     Nodo categoria B: un estado de error debe derivar en faultReport(CRITICAL). */
}

/**
 * @brief Handler de la trama del BMS / bateria (0x380).
 *
 * @param[in]  msg  Trama recibida.
 *
 * @return void
 */
static void handleBms(const twai_message_t *msg) {
  (void)msg;
  /* TODO: parsear tension, corriente y temperatura de celdas.
     Nodo categoria B: fuera de rango debe derivar en faultReport(CRITICAL). */
}

/**
 * @brief Handler de la trama de APPS + BSE (0x180).
 *
 * @param[in]  msg  Trama recibida.
 *
 * @return void
 */
static void handleAppsBse(const twai_message_t *msg) {
  (void)msg;
  /* TODO: parsear las dos señales de APPS y la presion de freno, escalarlas
     con sensorsScaleApps()/sensorsScaleBse() y evaluar las reglas de
     implausibilidad y de plausibilidad APPS/freno usando rules.h. */
}

/**
 * @brief Handler de las tramas de RPM de ruedas (0x200 y 0x201).
 *
 * @param[in]  msg  Trama recibida.
 *
 * @return void
 */
static void handleRpm(const twai_message_t *msg) {
  (void)msg;
  /* TODO: parsear RPM por rueda. Nodo categoria A: si falta, solo WARNING. */
}

/**
 * @brief Handler de la trama del volante (0x280).
 *
 * @param[in]  msg  Trama recibida.
 *
 * @return void
 */
static void handleSteering(const twai_message_t *msg) {
  (void)msg;
  /* TODO: parsear angulo y botonera. El boton de RTD tiene que terminar
     encolando un EcuEventType::RTD_REQUEST en qEvents. */
}

/**
 * @brief Handler de la trama de la IMU (0x400).
 *
 * @param[in]  msg  Trama recibida.
 *
 * @return void
 */
static void handleImu(const twai_message_t *msg) {
  (void)msg;
  /* TODO: parsear aceleraciones y velocidades angulares. Categoria A. */
}

/**
 * @brief Handler de la trama de TPMS (0x500).
 *
 * @param[in]  msg  Trama recibida.
 *
 * @return void
 */
static void handleTpms(const twai_message_t *msg) {
  (void)msg;
  /* TODO: parsear presion y temperatura por rueda. Categoria A. */
}
