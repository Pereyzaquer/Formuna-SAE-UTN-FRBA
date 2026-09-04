/************************************************************
 *  Proyecto : ECU
 *  Archivo  : logger.cpp
 *  Equipo   : UTN BA Motorsport Formula student team
 *  Fecha    : 2/9/2026
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Registrador de datos de la prueba. Guarda en memoria las
 *  muestras que llegan por CAN durante la corrida y las deja
 *  disponibles para exportarlas como CSV.
 *
 *  Hardware:
 *  --------------------------------------------------------
 *  - MCU: ESP32 S3.
 *  - Sensores: ninguno propio, recibe todo por CAN.
 *
 *  Notas:
 *  --------------------------------------------------------
 *  Cada muestra se guarda como una tupla (tiempo, canal, valor)
 *  y no como una fila con todas las columnas. Se hace asi porque
 *  cada nodo emite a su propio ritmo: el DHT11 una vez por
 *  segundo y el nodo de rueda veinte veces por segundo. Guardar filas
 *  completas obligaria a inventar valores para el canal que no
 *  hablo en ese instante, y a gastar memoria en columnas vacias.
 *  Las columnas se arman recien al generar el CSV.
 *
 ************************************************************/

/************************************************************
 *                     INCLUDES
 ************************************************************/
#include "../include/ECU.h"

/************************************************************
 *               CONSTANTES DEL SISTEMA
 ************************************************************/

/** Duracion de una corrida. Definida en 90 segundos para la prueba. */
static const uint32_t RECORDING_DURATION_MILLISECONDS = 90000;

/**
 * Cantidad maxima de muestras que entran en memoria.
 *
 * Con los dos nodos emitiendo a la vez son 21 muestras por segundo
 * (una del DHT11 mas veinte del nodo de rueda), que en 90 segundos
 * dan unas 1900. Se reservan 4000 para tener margen si despues se sube
 * la frecuencia de algun nodo. A 12 bytes por muestra son 48 kB, que
 * entran comodos en la memoria interna del ESP32-S3.
 */
static const uint32_t MAXIMUM_SAMPLE_COUNT = 4000;

/************************************************************
 *                       TIPOS
 ************************************************************/

/** Una muestra suelta: cuando, de que canal, y cuanto valia. */
struct LogSample {
  uint32_t   timeMilliseconds; /**< Desde el inicio de la grabacion. */
  LogChannel channel;
  float      value;
};

/************************************************************
 *                VARIABLES GLOBALES
 ************************************************************/
static LogSample samples[MAXIMUM_SAMPLE_COUNT];
static uint32_t  sampleCount = 0;
static uint32_t  recordingStartMilliseconds = 0;
static bool      recording = false;

/* Ultimo valor recibido de cada canal, se grabe o no. Sirve para que la
   pagina web muestre el sensor en vivo antes de arrancar la corrida, que
   es como se comprueba que el cableado esta bien sin gastar una prueba. */
static float currentValues[static_cast<uint8_t>(LogChannel::COUNT)] = {};
static bool  currentValueIsValid[static_cast<uint8_t>(LogChannel::COUNT)] = {};

/**
 * @brief Arranca una grabacion nueva y descarta la anterior.
 *
 * @return void
 */
void loggerStart(void) {
  sampleCount = 0;
  recordingStartMilliseconds = millis();
  recording = true;
}

/**
 * @brief Corta la grabacion en curso y conserva lo grabado.
 *
 * @return void
 */
void loggerStop(void) {
  recording = false;
}

/**
 * @brief Indica si hay una grabacion en curso.
 *
 * La grabacion tambien termina sola al cumplirse la duracion o al
 * llenarse la memoria, sin que nadie llame a loggerStop().
 *
 * @return bool  true mientras se este grabando.
 */
bool loggerIsRecording(void) {
  if (!recording) {
    return false;
  }
  if (loggerGetElapsedMilliseconds() >= RECORDING_DURATION_MILLISECONDS) {
    return false;
  }
  if (sampleCount >= MAXIMUM_SAMPLE_COUNT) {
    return false;
  }
  return true;
}

/**
 * @brief Informa un valor nuevo de un canal.
 *
 * El valor siempre queda como valor actual del canal, haya grabacion o
 * no. Ademas se agrega a la grabacion si hay una en curso.
 *
 * Los handlers de CAN pueden llamarla siempre, sin preguntar por el
 * estado del registrador: la decision de guardar o no se toma aca.
 *
 * @param[in]  channel  Canal al que pertenece el valor.
 * @param[in]  value    Valor ya convertido a unidad de ingenieria.
 *
 * @return void
 */
void loggerRecordValue(LogChannel channel, float value) {
  uint8_t channelIndex = static_cast<uint8_t>(channel);

  currentValues[channelIndex]       = value;
  currentValueIsValid[channelIndex] = true;

  if (!loggerIsRecording()) {
    return;
  }

  samples[sampleCount].timeMilliseconds = loggerGetElapsedMilliseconds();
  samples[sampleCount].channel          = channel;
  samples[sampleCount].value            = value;
  sampleCount++;
}

/**
 * @brief Devuelve el ultimo valor recibido de un canal.
 *
 * @param[in]   channel  Canal a consultar.
 * @param[out]  value    Ultimo valor recibido.
 *
 * @return bool  false si ese canal no recibio ningun valor todavia.
 */
bool loggerGetCurrentValue(LogChannel channel, float *value) {
  uint8_t channelIndex = static_cast<uint8_t>(channel);

  if (!currentValueIsValid[channelIndex]) {
    return false;
  }

  *value = currentValues[channelIndex];
  return true;
}

/**
 * @brief Milisegundos transcurridos de la grabacion en curso.
 *
 * @return uint32_t  Cero si no se grabo nada todavia.
 */
uint32_t loggerGetElapsedMilliseconds(void) {
  if (recordingStartMilliseconds == 0) {
    return 0;
  }
  return millis() - recordingStartMilliseconds;
}

/**
 * @brief Cantidad de muestras guardadas.
 *
 * @return uint32_t  Cantidad de muestras.
 */
uint32_t loggerGetSampleCount(void) {
  return sampleCount;
}

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
                     LogChannel *channel, float *value) {
  if (index >= sampleCount) {
    return false;
  }

  *timeMilliseconds = samples[index].timeMilliseconds;
  *channel          = samples[index].channel;
  *value            = samples[index].value;
  return true;
}

/**
 * @brief Devuelve el nombre de columna de un canal para el CSV.
 *
 * El nombre lleva la unidad adentro porque el CSV no tiene otro lugar
 * donde ponerla, y una columna de numeros sin unidad no sirve.
 *
 * @param[in]  channel  Canal a nombrar.
 *
 * @return const char*  Nombre con la unidad incluida.
 */
const char *loggerGetChannelName(LogChannel channel) {
  switch (channel) {
    case LogChannel::TEMPERATURE_CELSIUS:  return "temperatura_C";
    case LogChannel::HUMIDITY_PERCENT:     return "humedad_pct";
    case LogChannel::ACCELERATOR_PERCENT:  return "acelerador_pct";
    case LogChannel::BRAKE_PRESSURE_BAR:   return "freno_bar";
    case LogChannel::WHEEL_RPM_FRONT_LEFT: return "rpm_delantera_izq";
    case LogChannel::COUNT:                break;
  }
  return "desconocido";
}
