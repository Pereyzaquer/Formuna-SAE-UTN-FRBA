/************************************************************
 *  Proyecto : ECU
 *  Archivo  : ECU.h
 *  Equipo   : UTN BA Motorsport Formula student team
 *  Fecha    : 23/12/2025
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Encabezado principal del codigo. Contiene los tipos
 *  comunes a todos los modulos y los prototipos de las
 *  funciones publicas de cada archivo fuente.
 *
 *  Notas:
 *  --------------------------------------------------------
 *  Los umbrales de reglamento NO van aca: viven en rules.h.
 *  Los identificadores del bus CAN tampoco: viven en can_ids.h.
 *
 ************************************************************/
#pragma once

/************************************************************
 *                     INCLUDES
 ************************************************************/
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <Adafruit_NeoPixel.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <driver/twai.h>

/************************************************************
 *                     DEFINES
 ************************************************************/
#define SSID          "ESP32 S3 ECU"
#define PASSWORD      "12345678"
#define OTA_PASSWORD  "utnsae2026$"

#define RGB_PIN    48
#define NUM_PIXELS 1

/* TODO: confirmar contra el esquematico de la BaseBoard cuales son los
   GPIO realmente ruteados al transceiver CAN antes de la primera prueba
   sobre el auto. Estos dos valores son provisorios. */
#define CAN_TX_PIN 4
#define CAN_RX_PIN 5

/************************************************************
 *                       TIPOS
 ************************************************************/

/**
 * @brief Estados posibles del vehiculo.
 *
 * FAULT queda primero y con valor 0 para que cualquier variable
 * de estado sin inicializar caiga del lado seguro.
 */
enum class EcuState : uint8_t {
  FAULT,      /**< Falla latcheada. Torque cortado. Sin salida por software. */
  BOOT,       /**< Arranque: chequeo de presencia de los nodos del bus.      */
  CONFIG,     /**< Configuracion de los nodos que respondieron en BOOT.      */
  CAR_READY,  /**< Auto listo, torque inhibido, esperando secuencia RTD.     */
  CAR_ON      /**< Torque habilitado.                                        */
};

/**
 * @brief Severidad de una falla registrada.
 *
 * WARNING no frena la marcha (tipicamente un nodo categoria A caido).
 * CRITICAL fuerza la transicion a FAULT desde cualquier estado.
 */
enum class FaultLevel : uint8_t {
  NONE,
  WARNING,
  CRITICAL
};

/**
 * @brief Presencia confirmada de cada nodo del bus CAN.
 *
 * Cada sensor vive en su propia placa y reporta a la ECU por CAN, asi que
 * "presente" significa que contesto durante BOOT, no que haya un sensor
 * cableado a este micro.
 *
 * Categoria A: informativo. Si no responde se saltea su configuracion,
 *              se registra WARNING y el auto arranca igual.
 * Categoria B: seguridad. Si no responde o manda valores implausibles
 *              se va a FAULT y se corta el torque.
 */
struct SensorFlags {
  /* ---- Categoria B (seguridad) ---- */
  bool driver;   /**< Driver / inversor de traccion. */
  bool bms;      /**< BMS / pack de bateria.         */
  bool apps;     /**< Pedal acelerador (doble señal).*/
  bool bse;      /**< Sensor de presion de freno.    */

  /* ---- Categoria A (informativo) ---- */
  bool rpmFront; /**< RPM ruedas delanteras.         */
  bool rpmRear;  /**< RPM ruedas traseras.           */
  bool steering; /**< Sensor de angulo de volante.   */
  bool imu;      /**< IMU (giroscopo + acelerometro).*/
  bool tpms;     /**< Presion y temperatura de neumaticos. */
};

/**
 * @brief Tipos de evento que las tasks le mandan a la maquina de estados.
 */
enum class EcuEventType : uint8_t {
  RTD_REQUEST,     /**< Pedido de Ready To Drive desde el volante. */
  SHUTDOWN_REQUEST /**< Pedido de apagado de torque.               */
};

/**
 * @brief Evento hacia la task de estados.
 *
 * Es la unica via de comunicacion hacia la maquina de estados: nada de
 * variables globales sueltas escritas desde otra task.
 */
struct EcuEvent {
  EcuEventType type;
  uint32_t     data;
};

/**
 * @brief Comandos hacia la task de OTA.
 */
enum class OtaCmd : uint8_t {
  ENABLE,  /**< Levantar AP + OTA (solo valido en BOOT y CONFIG). */
  DISABLE  /**< Apagar WiFi. Irreversible hasta el proximo reset. */
};

/************************************************************
 *                VARIABLES GLOBALES EXTERNAS
 ************************************************************/
extern QueueHandle_t qEvents; /**< Eventos hacia taskState.  */
extern QueueHandle_t qOta;    /**< Comandos hacia taskOta.   */

/************************************************************
 *             PROTOTIPOS DE FUNCIONES
 ************************************************************/

/* ---------------------- state.cpp ---------------------- */

/**
 * @brief Task de la maquina de estados del vehiculo.
 *
 * @param[in]  arg  No se usa. Requerido por la firma de FreeRTOS.
 *
 * @return void
 */
void taskState(void *arg);

/**
 * @brief Devuelve el estado actual del vehiculo.
 *
 * Lectura de una sola palabra, por eso se expone directo y no por queue.
 *
 * @return EcuState  Estado vigente.
 */
EcuState stateGet(void);

/**
 * @brief Devuelve el nombre imprimible de un estado.
 *
 * Los nombres viven en un solo lugar para que el serial y la pagina web
 * no se contradigan cuando se agregue o renombre un estado.
 *
 * @param[in]  state  Estado a nombrar.
 *
 * @return const char*  Nombre en mayusculas, sin espacios.
 */
const char *stateName(EcuState state);

/* ----------------------- can.cpp ----------------------- */

/**
 * @brief Task de servicio del bus CAN: recibe, despacha y transmite.
 *
 * @param[in]  arg  No se usa. Requerido por la firma de FreeRTOS.
 *
 * @return void
 */
void taskCan(void *arg);

/**
 * @brief Inicializa el periferico TWAI a 500 kbps en modo normal.
 *
 * @return bool  true si el driver quedo instalado y arrancado.
 */
bool canInit(void);

/**
 * @brief Transmite una trama estandar por el bus.
 *
 * @param[in]  id    Identificador CAN de 11 bits (ver can_ids.h).
 * @param[in]  data  Puntero a los bytes a enviar.
 * @param[in]  len   Cantidad de bytes, 0 a 8.
 *
 * @return bool  true si la trama entro en la cola de TX del driver.
 */
bool canSend(uint32_t id, const uint8_t *data, uint8_t len);

/**
 * @brief Recibe una trama del bus.
 *
 * @param[out] msg          Trama recibida.
 * @param[in]  timeoutMs    Tiempo maximo de espera en milisegundos.
 *
 * @return bool  true si se recibio una trama.
 */
bool canReceive(twai_message_t *msg, uint32_t timeoutMs);

/**
 * @brief Despacha una trama recibida al handler que corresponde a su ID.
 *
 * @param[in]  msg  Trama recibida.
 *
 * @return void
 */
void canDispatch(const twai_message_t *msg);

/**
 * @brief Devuelve cuantas tramas se recibieron desde el arranque.
 *
 * Sirve para distinguir "el bus esta mudo" de "el bus anda pero los
 * datos no son los que espero", que desde afuera se parecen bastante.
 *
 * @return uint32_t  Cantidad de tramas recibidas.
 */
uint32_t canGetRxCount(void);

/* --------------------- sensors.cpp --------------------- */

/**
 * @brief Marca en las flags que nodo reporto presencia en el bus.
 *
 * @param[in]  canId  ID CAN del mensaje que llego.
 *
 * @return void
 */
void sensorsMarkSeen(uint32_t canId);

/**
 * @brief Devuelve las flags de presencia acumuladas.
 *
 * @return SensorFlags  Copia de las flags vigentes.
 */
SensorFlags sensorsGetFlags(void);

/**
 * @brief Envia la configuracion inicial a cada nodo presente.
 *
 * Los nodos ausentes de categoria A se saltean; los de categoria B no
 * llegan a este punto porque la falta de respuesta ya mando a FAULT.
 *
 * @return void
 */
void sensorsConfigure(void);

/**
 * @brief Escala la lectura cruda del acelerador a recorrido de pedal.
 *
 * @param[in]  raw  Valor crudo recibido por CAN.
 *
 * @return float  Recorrido de pedal en porcentaje, 0.0 a 100.0.
 */
float sensorsScaleApps(uint16_t raw);

/**
 * @brief Escala la lectura cruda del freno a presion de linea.
 *
 * @param[in]  raw  Valor crudo recibido por CAN.
 *
 * @return float  Presion en bar.
 */
float sensorsScaleBse(uint16_t raw);

/* -------------------- indicators.cpp ------------------- */

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

/**
 * @brief Refresca el led de debug segun el estado del vehiculo.
 *
 * Pensada para llamarse periodicamente: el pulso de CAR_ON depende de
 * cada cuanto se la invoque.
 *
 * @param[in]  state  Estado vigente.
 *
 * @return void
 */
void indicatorsUpdate(EcuState state);

/* ---------------------- fault.cpp ---------------------- */

/**
 * @brief Registra una falla y, si es CRITICAL, la latchea.
 *
 * @param[in]  level  Severidad de la falla.
 * @param[in]  code   Codigo propio del equipo para identificarla.
 *
 * @return void
 */
void faultReport(FaultLevel level, uint16_t code);

/**
 * @brief Indica si hay una falla CRITICAL latcheada.
 *
 * Una vez latcheada solo se limpia con un reset del micro: es
 * deliberado que no exista una funcion para bajarla por software.
 *
 * @return bool  true si el vehiculo debe permanecer en FAULT.
 */
bool faultIsLatched(void);

/**
 * @brief Devuelve el nivel de la ultima falla registrada.
 *
 * @return FaultLevel  Nivel vigente.
 */
FaultLevel faultGetLevel(void);

/**
 * @brief Devuelve el codigo de la ultima falla registrada.
 *
 * @return uint16_t  Codigo, o 0 si no hubo fallas.
 */
uint16_t faultGetCode(void);

/**
 * @brief Devuelve el nombre imprimible de un nivel de falla.
 *
 * @param[in]  level  Nivel a nombrar.
 *
 * @return const char*  Nombre en mayusculas.
 */
const char *faultLevelName(FaultLevel level);

/* --------------------- webmon.cpp ---------------------- */

/**
 * @brief Levanta el servidor web de monitoreo en el puerto 80.
 *
 * @return void
 */
void webmonBegin(void);

/**
 * @brief Baja el servidor web de monitoreo.
 *
 * @return void
 */
void webmonStop(void);

/**
 * @brief Atiende las peticiones web pendientes.
 *
 * Hay que llamarla seguido: el servidor no tiene task propia.
 *
 * @return void
 */
void webmonHandle(void);

/* ----------------------- ota.cpp ----------------------- */

/**
 * @brief Realiza el setup de la configuracion OTA
 *
 * @return void
 */
void otaSetup(void);

/**
 * @brief Task de OTA: atiende actualizaciones mientras esten habilitadas.
 *
 * @param[in]  arg  No se usa. Requerido por la firma de FreeRTOS.
 *
 * @return void
 */
void taskOta(void *arg);
