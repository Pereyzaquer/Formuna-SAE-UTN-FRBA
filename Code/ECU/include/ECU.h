/************************************************************
 *  Proyecto : ECU
 *  Archivo  : ECU.h
 *  Equipo   : UTN BA Motorsport Formula student team
 *  Fecha    : 23/12/2025
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Encabezado principal del codigo. Tipos comunes a todos los
 *  modulos y prototipos de las funciones publicas de cada
 *  archivo fuente.
 *
 *  Notas:
 *  --------------------------------------------------------
 *  Los identificadores del bus CAN viven en can_ids.h y los
 *  umbrales de reglamento en rules.h. Aca no se repite ninguno.
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
 *                  RED Y ACTUALIZACION
 ************************************************************/
#define WIFI_NETWORK_NAME  "ESP32 S3 ECU"
#define WIFI_PASSWORD      "12345678"
#define OTA_PASSWORD       "utnsae2026$"

/************************************************************
 *                      PINES
 ************************************************************/
#define RGB_LED_PIN        48  /**< Led RGB de debug incluido en la placa. */
#define RGB_LED_COUNT      1

/* Definidos para la prueba de banco. Pendiente confirmarlos contra el
   esquematico de la BaseBoard antes de montar la ECU en el auto. */
#define CAN_TRANSMIT_PIN   4   /**< Al pin TX del transceiver CAN. */
#define CAN_RECEIVE_PIN    5   /**< Al pin RX del transceiver CAN. */

/* Boton que simula la secuencia de Ready To Drive. Se lee con la
   resistencia de pull-up interna, asi que descansa en alto y se pone
   en bajo al apretarlo: el otro extremo del boton va a masa. */
#define READY_TO_DRIVE_BUTTON_PIN 6

/************************************************************
 *                       TIPOS
 ************************************************************/

/**
 * @brief Estados posibles del vehiculo.
 *
 * FAULT queda primero y con valor 0 para que cualquier variable de
 * estado sin inicializar caiga del lado seguro.
 */
enum class EcuState : uint8_t {
  FAULT,      /**< Falla latcheada. Torque cortado. Sin salida por software. */
  BOOT,       /**< Arranque: se busca que nodos hay en el bus.               */
  CONFIG,     /**< Configuracion de los nodos que respondieron.              */
  CAR_READY,  /**< Listo, torque inhibido, esperando el boton de RTD.        */
  CAR_ON      /**< Torque habilitado. Durante la prueba, grabando datos.     */
};

/**
 * @brief Severidad de una falla registrada.
 *
 * WARNING no frena la marcha, tipicamente un nodo informativo caido.
 * CRITICAL manda el vehiculo a FAULT desde cualquier estado.
 */
enum class ErrorLevel : uint8_t {
  NONE,
  WARNING,
  CRITICAL
};

/**
 * @brief Presencia confirmada de cada nodo del bus CAN.
 *
 * Cada sensor vive en su propia placa y reporta por CAN, asi que
 * "presente" significa que emitio al menos una trama, no que haya un
 * sensor cableado a este micro.
 *
 * Categoria A, informativo: si no responde se marca la flag, se saltea
 *   su configuracion, se registra WARNING y el auto sigue.
 * Categoria B, seguridad: si no responde o manda valores implausibles
 *   se pasa a FAULT y se corta el torque.
 */
struct SensorFlags {
  /* ---- Categoria B, seguridad ---- */
  bool driver;              /**< Driver / inversor de traccion.  */
  bool batteryManagement;   /**< BMS / pack de bateria.          */
  bool accelerator;         /**< APPS, pedal acelerador.         */
  bool brake;               /**< BSE, presion de freno.          */

  /* ---- Categoria A, informativo ---- */
  bool wheelSpeedFront;     /**< RPM ruedas delanteras.          */
  bool wheelSpeedRear;      /**< RPM ruedas traseras.            */
  bool motorSpeed;          /**< RPM en el motor / diferencial.  */
  bool steeringWheel;       /**< Angulo de volante.              */
  bool inertialUnit;        /**< IMU.                            */
  bool tirePressure;        /**< TPMS.                           */

  /* ---- Nodo de la prueba de banco ---- */
  bool testTemperature;     /**< Nodo ESP32-C3 con DHT11.        */
};

/**
 * @brief Canales que el registrador guarda durante la prueba.
 *
 * La lista es fija a proposito: el archivo CSV sale siempre con las
 * mismas columnas, tenga datos o no. Asi se obtienen todos los graficos
 * aunque haya un solo nodo conectado, que es justo lo que se quiere
 * comprobar.
 *
 * COUNT tiene que quedar ultimo: vale como cantidad de canales.
 */
enum class LogChannel : uint8_t {
  TEMPERATURE_CELSIUS,     /**< Nodo DHT11.               */
  HUMIDITY_PERCENT,        /**< Nodo DHT11.               */
  WHEEL_RPM_FRONT_LEFT,    /**< Nodo de velocidad LM393.  */
  ACCELERATOR_PERCENT,     /**< Sin nodo todavia, queda vacio. */
  BRAKE_PRESSURE_BAR,      /**< Sin nodo todavia, queda vacio. */
  COUNT
};

/**
 * @brief Tipos de evento que las otras tasks le mandan a los estados.
 */
enum class EcuEventType : uint8_t {
  READY_TO_DRIVE_REQUEST, /**< Se apreto el boton de RTD.  */
  SHUTDOWN_REQUEST        /**< Se pidio apagar el torque.  */
};

/**
 * @brief Evento hacia la task de estados.
 *
 * Es la unica via de entrada a la maquina de estados: ninguna otra task
 * escribe variables sueltas que ella lea.
 */
struct EcuEvent {
  EcuEventType type;
  uint32_t     data;
};

/**
 * @brief Comandos hacia la task de wifi y actualizacion.
 */
enum class WifiCommand : uint8_t {
  ENABLE,  /**< Levantar el punto de acceso, OTA y monitoreo web. */
  DISABLE  /**< Apagar la radio.                                  */
};

/************************************************************
 *                VARIABLES GLOBALES EXTERNAS
 ************************************************************/
extern QueueHandle_t queueEvents; /**< Eventos hacia taskState. */
extern QueueHandle_t queueWifi;   /**< Comandos hacia taskWifi. */

/************************************************************
 *             PROTOTIPOS DE FUNCIONES
 ************************************************************/

/* ---------------------- state.cpp ---------------------- */

/**
 * @brief Task de la maquina de estados del vehiculo.
 *
 * @param[in]  argument  No se usa. Lo exige la firma de FreeRTOS.
 *
 * @return void
 */
void taskState(void *argument);

/**
 * @brief Devuelve el estado actual del vehiculo.
 *
 * @return EcuState  Estado vigente.
 */
EcuState stateGet(void);

/**
 * @brief Devuelve el nombre imprimible de un estado.
 *
 * Los nombres viven en un solo lugar para que el puerto serie y la
 * pagina web no se contradigan si se agrega o renombra un estado.
 *
 * @param[in]  state  Estado a nombrar.
 *
 * @return const char*  Nombre en mayusculas, sin espacios.
 */
const char *stateGetName(EcuState state);

/* ----------------------- can.cpp ----------------------- */

/**
 * @brief Task de servicio del bus CAN: recibe, despacha y transmite.
 *
 * @param[in]  argument  No se usa. Lo exige la firma de FreeRTOS.
 *
 * @return void
 */
void taskCan(void *argument);

/**
 * @brief Inicializa el periferico TWAI a 500 kbps en modo normal.
 *
 * @return bool  true si el driver quedo instalado y arrancado.
 */
bool canInitialize(void);

/**
 * @brief Transmite una trama estandar por el bus.
 *
 * @param[in]  identifier  Identificador CAN de 11 bits, ver can_ids.h.
 * @param[in]  data        Puntero a los bytes a enviar.
 * @param[in]  length      Cantidad de bytes, de 0 a 8.
 *
 * @return bool  true si la trama entro en la cola de transmision.
 */
bool canSendFrame(uint32_t identifier, const uint8_t *data, uint8_t length);

/**
 * @brief Despacha una trama recibida al handler de su identificador.
 *
 * @param[in]  frame  Trama recibida.
 *
 * @return void
 */
void canDispatchFrame(const twai_message_t *frame);

/**
 * @brief Cuantas veces hubo que reenganchar el controlador al bus.
 *
 * Si este numero crece sin parar, la ECU esta sola en el bus o el
 * cableado esta mal: nadie le esta confirmando lo que transmite.
 *
 * @return uint32_t  Cantidad de recuperaciones.
 */
uint32_t canGetBusRecoveryCount(void);

/**
 * @brief Devuelve cuantas tramas se recibieron desde el arranque.
 *
 * Sirve para distinguir "el bus esta mudo" de "el bus anda pero los
 * datos no son los que espero", que desde afuera se parecen bastante.
 *
 * @return uint32_t  Cantidad de tramas recibidas.
 */
uint32_t canGetReceivedFrameCount(void);

/* --------------------- sensors.cpp --------------------- */

/**
 * @brief Marca en las flags que nodo emitio una trama.
 *
 * @param[in]  identifier  Identificador CAN de la trama que llego.
 *
 * @return void
 */
void sensorsMarkNodeSeen(uint32_t identifier);

/**
 * @brief Devuelve las flags de presencia acumuladas.
 *
 * @return SensorFlags  Copia de las flags vigentes.
 */
SensorFlags sensorsGetFlags(void);

/**
 * @brief Cuenta cuantos nodos distintos se detectaron en el bus.
 *
 * @return uint8_t  Cantidad de nodos presentes.
 */
uint8_t sensorsCountPresentNodes(void);

/* ---------------------- logger.cpp --------------------- */

/**
 * @brief Arranca una grabacion nueva y descarta la anterior.
 *
 * @return void
 */
void loggerStart(void);

/**
 * @brief Corta la grabacion en curso y conserva lo grabado.
 *
 * @return void
 */
void loggerStop(void);

/**
 * @brief Indica si hay una grabacion en curso.
 *
 * @return bool  true mientras se este grabando.
 */
bool loggerIsRecording(void);

/**
 * @brief Informa un valor nuevo de un canal.
 *
 * El valor siempre queda como valor actual del canal, haya grabacion o
 * no, y ademas se agrega a la grabacion si hay una en curso.
 *
 * @param[in]  channel  Canal al que pertenece el valor.
 * @param[in]  value    Valor ya convertido a unidad de ingenieria.
 *
 * @return void
 */
void loggerRecordValue(LogChannel channel, float value);

/**
 * @brief Devuelve el ultimo valor recibido de un canal.
 *
 * @param[in]   channel  Canal a consultar.
 * @param[out]  value    Ultimo valor recibido.
 *
 * @return bool  false si ese canal no recibio ningun valor todavia.
 */
bool loggerGetCurrentValue(LogChannel channel, float *value);

/**
 * @brief Milisegundos transcurridos de la grabacion en curso.
 *
 * @return uint32_t  Cero si no se grabo nada todavia.
 */
uint32_t loggerGetElapsedMilliseconds(void);

/**
 * @brief Cantidad de muestras guardadas.
 *
 * @return uint32_t  Cantidad de muestras.
 */
uint32_t loggerGetSampleCount(void);

/**
 * @brief Lee una muestra guardada.
 *
 * @param[in]   index              Numero de muestra, desde 0.
 * @param[out]  timeMilliseconds   Tiempo desde el inicio de la grabacion.
 * @param[out]  channel            Canal de la muestra.
 * @param[out]  value              Valor de la muestra.
 *
 * @return bool  false si el indice esta fuera de rango.
 */
bool loggerGetSample(uint32_t index, uint32_t *timeMilliseconds,
                     LogChannel *channel, float *value);

/**
 * @brief Devuelve el nombre de columna de un canal para el CSV.
 *
 * @param[in]  channel  Canal a nombrar.
 *
 * @return const char*  Nombre con la unidad incluida.
 */
const char *loggerGetChannelName(LogChannel channel);

/* -------------------- indicators.cpp ------------------- */

/**
 * @brief Inicializa el led RGB de la placa.
 *
 * @return void
 */
void indicatorsInitialize(void);

/**
 * @brief Enciende el led RGB con un color.
 *
 * @param[in]  red    Componente rojo, 0 a 255.
 * @param[in]  green  Componente verde, 0 a 255.
 * @param[in]  blue   Componente azul, 0 a 255.
 *
 * @return void
 */
void indicatorsSetColor(uint8_t red, uint8_t green, uint8_t blue);

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

/* ---------------------- errors.cpp ---------------------- */

/**
 * @brief Registra una falla y, si es CRITICAL, la deja latcheada.
 *
 * @param[in]  level  Severidad de la falla.
 * @param[in]  code   Codigo propio del equipo para identificarla.
 *
 * @return void
 */
void errorReport(ErrorLevel level, uint16_t code);

/**
 * @brief Indica si hay una falla CRITICAL latcheada.
 *
 * Una vez latcheada solo se limpia reiniciando el micro: es deliberado
 * que no exista una funcion para bajarla por software.
 *
 * @return bool  true si el vehiculo debe permanecer en FAULT.
 */
bool errorIsLatched(void);

/**
 * @brief Devuelve el nivel de la ultima falla registrada.
 *
 * @return ErrorLevel  Nivel vigente.
 */
ErrorLevel errorGetLevel(void);

/**
 * @brief Devuelve el codigo de la ultima falla registrada.
 *
 * @return uint16_t  Codigo, o 0 si no hubo fallas.
 */
uint16_t errorGetCode(void);

/**
 * @brief Devuelve el nombre imprimible de un nivel de falla.
 *
 * @param[in]  level  Nivel a nombrar.
 *
 * @return const char*  Nombre en mayusculas.
 */
const char *errorGetLevelName(ErrorLevel level);

/* ----------------------- wifi.cpp ----------------------- */

/**
 * @brief Task de wifi: punto de acceso, actualizacion OTA y web.
 *
 * @param[in]  argument  No se usa. Lo exige la firma de FreeRTOS.
 *
 * @return void
 */
void taskWifi(void *argument);

/* --------------------- webmonitor.cpp ---------------------- */

/**
 * @brief Levanta el servidor web de monitoreo en el puerto 80.
 *
 * @return void
 */
void webMonitorStart(void);

/**
 * @brief Baja el servidor web de monitoreo.
 *
 * @return void
 */
void webMonitorStop(void);

/**
 * @brief Atiende las peticiones web pendientes.
 *
 * Hay que llamarla seguido: el servidor no tiene task propia.
 *
 * @return void
 */
void webMonitorHandleRequests(void);
