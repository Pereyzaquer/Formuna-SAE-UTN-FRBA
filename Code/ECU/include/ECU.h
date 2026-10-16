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

/* Hay dos formas de colgar la ECU del bus, y el entorno de platformio.ini
   elige cual se compila. Solo se usa un juego de pines a la vez.

   CAN nativo del ESP32 (can_twai.cpp), el definitivo para el auto: el
   controlador esta adentro del chip y afuera va solo un transceiver de
   3.3 V, tipo SN65HVD230.
   Pendiente confirmar estos pines contra el esquematico de la BaseBoard. */
#define CAN_TRANSMIT_PIN   4   /**< Al pin TX del transceiver CAN. */
#define CAN_RECEIVE_PIN    5   /**< Al pin RX del transceiver CAN. */

/* Modulo MCP2515 por SPI (can_mcp2515.cpp), para la prueba de banco: es
   lo que hay a mano. El modulo es de 5 V y el ESP32 de 3.3 V, asi que
   las cuatro lineas pasan por un conversor de nivel. Ver ARQUITECTURA.md. */
#define CAN_SPI_CLOCK_PIN        12  /**< SCK  del MCP2515. */
#define CAN_SPI_MASTER_OUT_PIN   11  /**< SI   del MCP2515. */
#define CAN_SPI_MASTER_IN_PIN    13  /**< SO   del MCP2515. */
#define CAN_SPI_CHIP_SELECT_PIN  10  /**< CS   del MCP2515. */

/* Boton que simula la secuencia de Ready To Drive. Se lee con la
   resistencia de pull-up interna, asi que descansa en alto y se pone
   en bajo al apretarlo: el otro extremo del boton va a masa. */
#define READY_TO_DRIVE_BUTTON_PIN 6

/* Pantalla TFT de 4" (MSP4020, controlador ST7796S). Va en un bus SPI
   propio, separado del MCP2515, para que redibujar no le quite tiempo
   al CAN. Es de 3.3 V: va directo, sin conversor. Los pines se le pasan
   a la libreria TFT_eSPI desde platformio.ini; estos nombres son solo
   para documentarlos en un mismo lugar con el resto. */
#define DISPLAY_SPI_CLOCK_PIN       18  /**< SCK  de la pantalla. */
#define DISPLAY_SPI_MASTER_OUT_PIN  17  /**< SDI  de la pantalla. */
#define DISPLAY_CHIP_SELECT_PIN     15  /**< CS   de la pantalla. */
#define DISPLAY_DATA_COMMAND_PIN    16  /**< DC/RS de la pantalla. */
#define DISPLAY_RESET_PIN            7  /**< RESET de la pantalla. */

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
  bool testTemperature;     /**< Nodo NodeMCU con DHT11.         */
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
  TEMPERATURE_CELSIUS,     /**< Nodo DHT11.                    */
  HUMIDITY_PERCENT,        /**< Nodo DHT11.                    */
  WHEEL_RPM_FRONT_LEFT,    /**< Nodo de RPM delantero.         */
  WHEEL_RPM_FRONT_RIGHT,   /**< Nodo de RPM delantero.         */
  WHEEL_RPM_REAR_LEFT,     /**< Nodo de RPM trasero.           */
  WHEEL_RPM_REAR_RIGHT,    /**< Nodo de RPM trasero.           */
  VEHICLE_SPEED_KPH,       /**< Calculado por la ECU.          */
  ACCELERATOR_PERCENT,     /**< Sin nodo todavia, queda vacio. */
  BRAKE_PRESSURE_BAR,      /**< Sin nodo todavia, queda vacio. */
  DROPS_WHEEL_FRONT,       /**< Veces que se callo el nodo delantero. */
  DROPS_WHEEL_REAR,        /**< Veces que se callo el nodo trasero.   */
  DROPS_TEMPERATURE,       /**< Veces que se callo el nodo DHT11.     */
  BATTERY_PERCENT,         /**< Estado de carga que informa el BMS.   */
  COUNT
};

/**
 * @brief Las cuatro ruedas del vehiculo.
 *
 * COUNT tiene que quedar ultimo: vale como cantidad de ruedas.
 */
enum class WheelPosition : uint8_t {
  FRONT_LEFT,
  FRONT_RIGHT,
  REAR_LEFT,
  REAR_RIGHT,
  COUNT
};

/**
 * @brief Una trama CAN, independiente del hardware que la mueve.
 *
 * Existe para que el resto del programa no dependa de si el bus lo
 * maneja el controlador interno del ESP32 o un MCP2515 externo: los dos
 * backends traducen a y desde esta estructura.
 */
struct CanFrame {
  uint32_t identifier; /**< Identificador de 11 bits, ver can_ids.h. */
  uint8_t  length;     /**< Cantidad de bytes utiles, 0 a 8.          */
  uint8_t  data[8];
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
 * @brief Indica si el controlador CAN esta andando.
 *
 * Si no arranco al encender, taskCan lo reintenta cada segundo.
 *
 * @return bool  true si el bus se puede usar.
 */
bool canIsReady(void);

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
void canDispatchFrame(const CanFrame *frame);

/**
 * @brief Velocidad del bus guardada, en kbps (125, 250, 500 o 1000).
 *
 * @return uint16_t  Velocidad que usa el controlador al arrancar.
 */
uint16_t canGetBitrateKilobits(void);

/**
 * @brief Guarda una velocidad de bus nueva. Se aplica al reiniciar.
 *
 * @param[in]  kilobits  125, 250, 500 o 1000; otro valor se ignora.
 *
 * @return bool  true si se guardo.
 */
bool canSetBitrateKilobits(uint16_t kilobits);

/**
 * @brief Elige de que trama y byte se lee el estado de carga del BMS.
 *
 * @param[in]  identifier       Identificador CAN de la trama.
 * @param[in]  byteIndex        Byte que tiene el estado de carga, 0 a 7.
 * @param[in]  percentPerCount  Por ciento que vale cada cuenta del byte.
 *
 * @return void
 */
void canSetBatteryLayout(uint32_t identifier, uint8_t byteIndex, float percentPerCount);

/**
 * @brief Identificador de la trama del estado de carga.
 *
 * @return uint32_t  Identificador vigente.
 */
uint32_t canGetBatteryIdentifier(void);

/**
 * @brief Byte de la trama que tiene el estado de carga.
 *
 * @return uint8_t  Indice, de 0 a 7.
 */
uint8_t canGetBatteryByte(void);

/**
 * @brief Por ciento que vale cada cuenta del byte de estado de carga.
 *
 * @return float  Escala vigente.
 */
float canGetBatteryPercentPerCount(void);

/**
 * @brief Estado de carga de la bateria, si el BMS esta informando.
 *
 * @param[out]  percent  Estado de carga, de 0 a 100.
 *
 * @return bool  false si nunca llego la trama o hace mas de 2 s que no llega.
 */
bool canGetBatteryPercent(float *percent);

/**
 * @brief Ultima trama de estado de carga tal como llego, para revisarla.
 *
 * @param[out]  frame                Copia de la trama.
 * @param[out]  ageMilliseconds      Hace cuanto llego.
 *
 * @return bool  false si todavia no llego ninguna.
 */
bool canGetLastBatteryFrame(CanFrame *frame, uint32_t *ageMilliseconds);

/* --------- can_twai.cpp  o  can_mcp2515.cpp ------------ */
/*
 * Capa de hardware del bus. Hay dos implementaciones con exactamente
 * estas cuatro funciones, y el entorno de platformio.ini compila una:
 *
 *   can_twai.cpp     controlador CAN interno del ESP32 + transceiver.
 *                    Es el definitivo para el auto.
 *   can_mcp2515.cpp  modulo MCP2515 externo por SPI. Para el banco,
 *                    porque es lo que hay.
 *
 * Todo lo demas de can.cpp es comun a las dos.
 */

/**
 * @brief Deja el controlador CAN andando a 500 kbps.
 *
 * @return bool  true si quedo listo para transmitir y recibir.
 */
bool canHardwareInitialize(void);

/**
 * @brief Entrega una trama al controlador para que la transmita.
 *
 * No espera a que salga al bus: si no hay lugar, la descarta.
 *
 * @param[in]  frame  Trama a transmitir.
 *
 * @return bool  true si el controlador la acepto.
 */
bool canHardwareSend(const CanFrame *frame);

/**
 * @brief Espera una trama entrante, como maximo el tiempo indicado.
 *
 * @param[out]  frame                Trama recibida.
 * @param[in]   timeoutMilliseconds  Cuanto esperar si no hay nada.
 *
 * @return bool  true si llego una trama.
 */
bool canHardwareReceive(CanFrame *frame, uint32_t timeoutMilliseconds);

/**
 * @brief Revisa si el controlador se cayo del bus y lo reengancha.
 *
 * @return bool  true si en esta llamada se detecto una caida.
 */
bool canHardwareRecoverIfNeeded(void);

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

/** Cuantos identificadores ajenos al mapa se recuerdan como maximo. */
#define UNKNOWN_IDENTIFIER_MAXIMUM 8

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

/**
 * @brief Revisa que nodos se callaron y cuenta cada caida.
 *
 * Hay que llamarla periodicamente. Un nodo cuenta como callado si ya
 * aparecio alguna vez y lleva mas del tiempo indicado sin emitir.
 *
 * @param[in]  silenceTimeoutMilliseconds  Cuanto silencio se tolera.
 *
 * @return uint32_t  Identificador CAN de un nodo que se acaba de callar
 *                   en esta llamada, o 0 si ninguno cayo ahora.
 */
uint32_t sensorsUpdateSilence(uint32_t silenceTimeoutMilliseconds);

/**
 * @brief Devuelve el primer nodo que esta callado en este momento.
 *
 * @return uint32_t  Identificador CAN, o 0 si todos emiten.
 */
uint32_t sensorsGetSilentNode(void);

/**
 * @brief Cuantas veces se callo un nodo desde que arranco la ECU.
 *
 * @param[in]  identifier  Identificador CAN del nodo.
 *
 * @return uint16_t  Cantidad de caidas.
 */
uint16_t sensorsGetDropCount(uint32_t identifier);

/**
 * @brief Cantidad de nodos del mapa (can_ids.h) que la ECU vigila.
 *
 * @return uint8_t  Cantidad.
 */
uint8_t sensorsGetKnownNodeCount(void);

/**
 * @brief Identificador CAN del nodo conocido en la posicion dada.
 *
 * @param[in]  index  De 0 a sensorsGetKnownNodeCount() - 1.
 *
 * @return uint32_t  Identificador.
 */
uint32_t sensorsGetKnownNodeIdentifier(uint8_t index);

/**
 * @brief Indica si un nodo emitio alguna vez desde el encendido.
 *
 * @param[in]  identifier  Identificador CAN.
 *
 * @return bool  true si se lo vio al menos una vez.
 */
bool sensorsNodeWasEverSeen(uint32_t identifier);

/**
 * @brief Indica si un nodo esta callado en este momento.
 *
 * @param[in]  identifier  Identificador CAN.
 *
 * @return bool  true si se lo vio y ahora no emite.
 */
bool sensorsNodeIsSilent(uint32_t identifier);

/**
 * @brief Anota un identificador que no figura en el mapa can_ids.h.
 *
 * Sirve para que un nodo nuevo aparezca en la pagina y se lo pueda
 * bautizar. Se recuerdan como maximo UNKNOWN_IDENTIFIER_MAXIMUM.
 *
 * @param[in]  identifier  Identificador CAN visto en el bus.
 *
 * @return void
 */
void sensorsNoteUnknownIdentifier(uint32_t identifier);

/**
 * @brief Cuantos identificadores desconocidos se vieron.
 *
 * @return uint8_t  Cantidad, hasta UNKNOWN_IDENTIFIER_MAXIMUM.
 */
uint8_t sensorsGetUnknownIdentifierCount(void);

/**
 * @brief Identificador desconocido en la posicion dada.
 *
 * @param[in]  index  De 0 a sensorsGetUnknownIdentifierCount() - 1.
 *
 * @return uint32_t  Identificador.
 */
uint32_t sensorsGetUnknownIdentifier(uint8_t index);

/**
 * @brief Cuantas tramas llegaron de un identificador desconocido.
 *
 * @param[in]  index  De 0 a sensorsGetUnknownIdentifierCount() - 1.
 *
 * @return uint32_t  Cantidad de tramas.
 */
uint32_t sensorsGetUnknownIdentifierFrameCount(uint8_t index);

/* ---------------------- names.cpp ---------------------- */

/** Largo maximo de un nombre de nodo, sin contar el terminador. */
#define NAME_MAXIMUM_LENGTH 24

/**
 * @brief Abre el almacenamiento de nombres. Llamar una vez al arrancar.
 *
 * @return void
 */
void namesInitialize(void);

/**
 * @brief Nombre legible de un identificador CAN.
 *
 * @param[in]   identifier  Identificador CAN.
 * @param[out]  name        Donde se copia el nombre guardado, el de
 *                          fabrica, o "sin definir".
 * @param[in]   nameSize    Tamano de name; NAME_MAXIMUM_LENGTH + 1 alcanza.
 *
 * @return void
 */
void namesGet(uint32_t identifier, char *name, size_t nameSize);

/**
 * @brief Guarda el nombre de un identificador en la flash.
 *
 * @param[in]  identifier  Identificador CAN.
 * @param[in]  name        Nombre nuevo; vacio vuelve al de fabrica.
 *
 * @return void
 */
void namesSet(uint32_t identifier, const char *name);

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
 * @brief Fija cuanto va a durar la proxima ejecucion.
 *
 * @param[in]  seconds  Duracion en segundos; se recorta al tope interno.
 *
 * @return void
 */
void loggerSetDurationSeconds(uint32_t seconds);

/**
 * @brief Duracion vigente para una ejecucion.
 *
 * @return uint32_t  Segundos.
 */
uint32_t loggerGetDurationSeconds(void);

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

/* ---------------------- speed.cpp ---------------------- */

/**
 * @brief Informa las vueltas por minuto medidas en una rueda.
 *
 * La convierte a velocidad y la guarda con la hora en que llego, para
 * poder distinguir despues un dato fresco de uno viejo.
 *
 * @param[in]  wheel                 Rueda que reporta.
 * @param[in]  revolutionsPerMinute  Vueltas por minuto medidas.
 *
 * @return void
 */
void speedUpdateWheel(WheelPosition wheel, uint16_t revolutionsPerMinute);

/**
 * @brief Devuelve la velocidad medida por una rueda.
 *
 * @param[in]   wheel  Rueda a consultar.
 * @param[out]  kph    Velocidad en kilometros por hora.
 *
 * @return bool  false si esa rueda no reporto hace demasiado tiempo.
 */
bool speedGetWheelKph(WheelPosition wheel, float *kph);

/**
 * @brief Cuantas ruedas tienen una medicion reciente.
 *
 * @return uint8_t  De 0 a 4.
 */
uint8_t speedGetValidWheelCount(void);

/**
 * @brief Indica si alguna rueda reporto alguna vez desde el arranque.
 *
 * Sirve para separar "nunca hubo sensores de rueda", que en el banco es
 * normal, de "los habia y se perdieron", que en el auto es una falla.
 *
 * @return bool  true si alguna rueda hablo alguna vez.
 */
bool speedAnyWheelEverSeen(void);

/**
 * @brief Calcula la velocidad del vehiculo a partir de las ruedas.
 *
 * Promedia las ruedas que tienen medicion reciente y descarta las que
 * se apartan demasiado del resto.
 *
 * @param[out]  kph  Velocidad en kilometros por hora.
 *
 * @return bool  false si no hay ninguna medicion confiable.
 */
bool speedGetVehicleKph(float *kph);

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

/*
 * Codigos de falla y aviso. Los usa la maquina de estados para
 * reportarlos y la pantalla y la web para describirlos. Definidos para
 * la prueba; pendiente unificarlos en una tabla del equipo.
 */
constexpr uint16_t ERROR_CODE_SAFETY_NODE_MISSING  = 0x0001; /**< Falta un nodo de seguridad.  */
constexpr uint16_t ERROR_CODE_NODE_LOST            = 0x0002; /**< Un nodo dejo de emitir.      */
constexpr uint16_t ERROR_CODE_ALL_WHEEL_SPEED_LOST = 0x0003; /**< Ninguna rueda mide.          */
constexpr uint16_t ERROR_CODE_BATTERY_MISSING      = 0x0004; /**< El BMS no informa la carga.  */
constexpr uint16_t ERROR_CODE_CAN_UNAVAILABLE      = 0x0005; /**< El controlador CAN no arranca. */

/**
 * @brief Descripcion corta de un codigo de falla, para pantalla y web.
 *
 * @param[in]  code  Codigo de falla.
 *
 * @return const char*  Texto en minusculas, sin acentos.
 */
const char *errorGetDescription(uint16_t code);


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
 * @brief Borra una WARNING cuando su causa desaparecio.
 *
 * No toca una CRITICAL: esas solo se van con errorClear() o un reset.
 *
 * @return void
 */
void errorClearWarning(void);

/**
 * @brief Borra cualquier falla, incluida una CRITICAL latcheada.
 *
 * Solo la llama la maquina de estados cuando decide que una falla
 * puede rearmarse sin reiniciar.
 *
 * @return void
 */
void errorClear(void);

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

/* ---------------------- display.cpp --------------------- */

/**
 * @brief Task de la pantalla: muestra estado, avisos y mediciones.
 *
 * @param[in]  argument  No se usa. Lo exige la firma de FreeRTOS.
 *
 * @return void
 */
void taskDisplay(void *argument);

/**
 * @brief Elige que canal alimenta la barra de RPM y su fondo de escala.
 *
 * @param[in]  channel    Canal del registrador.
 * @param[in]  fullScale  Valor que llena la barra; debe ser positivo.
 *
 * @return void
 */
void displaySetRevolutionsBar(LogChannel channel, float fullScale);

/**
 * @brief Fija la presion de freno que llena la barra de freno.
 *
 * @param[in]  fullScaleBar  Presion en bar; debe ser positiva.
 *
 * @return void
 */
void displaySetBrakeFullScale(float fullScaleBar);

/**
 * @brief Muestra en las barras los valores de los deslizables de la web.
 *
 * @param[in]  acceleratorPercent  Acelerador, de 0 a 100.
 * @param[in]  brakeBar            Freno, en bar.
 * @param[in]  revolutions         Valor para la barra de RPM.
 *
 * @return void
 */
void displaySetTestValues(float acceleratorPercent, float brakeBar, float revolutions);

/**
 * @brief Termina el modo prueba: las barras vuelven a los datos reales.
 *
 * @return void
 */
void displayStopTest(void);

/**
 * @brief Canal que alimenta la barra de RPM.
 *
 * @return LogChannel  Canal vigente.
 */
LogChannel displayGetRevolutionsChannel(void);

/**
 * @brief Valor que llena la barra de RPM.
 *
 * @return float  Fondo de escala vigente.
 */
float displayGetRevolutionsFullScale(void);

/**
 * @brief Presion de freno que llena la barra de freno.
 *
 * @return float  Fondo de escala vigente, en bar.
 */
float displayGetBrakeFullScale(void);

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
