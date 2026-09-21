/************************************************************
 *  Proyecto : ECU
 *  Archivo  : logger.cpp
 *  Equipo   : UTN BA Motorsport Formula student team
 *  Fecha    : 2/9/2026
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Registrador de datos de la prueba. Guarda en memoria las
 *  muestras que llegan por CAN durante la ejecucion y las deja
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

/**
 * Duracion de una ejecucion si nadie la cambia.
 *
 * 90 segundos, definidos para el banco; no salen de ningun reglamento.
 * Se cambia desde la pagina web antes de arrancar, hasta el tope de
 * abajo.
 */
static const uint32_t DEFAULT_DURATION_SECONDS = 90;

/**
 * Tope de duracion. Con 80 muestras por segundo, MAXIMUM_SAMPLE_COUNT
 * alcanza para unos 250 s; se deja margen por si se suman nodos.
 */
static const uint32_t MAXIMUM_DURATION_SECONDS = 180;

/**
 * Cantidad maxima de muestras que entran en memoria.
 *
 * Con los tres nodos del banco son unas 80 muestras por segundo: 20 de
 * cada rueda, una de temperatura y una de velocidad por cada trama de
 * rueda. En 90 segundos son unas 7300; se reservan 20000 (unos cuatro
 * minutos) para cuando se sumen nodos. A 12 bytes por muestra son
 * 240 kB, que van a la PSRAM (8 MB en el ESP32-S3 N16R8) y no a la RAM
 * interna, que la necesita el wifi.
 */
static const uint32_t MAXIMUM_SAMPLE_COUNT = 20000;

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
/* Se reserva en la PSRAM la primera vez que arranca una grabacion, y
   queda reservado para siempre. */
static LogSample *samples = nullptr;

static uint32_t durationSeconds = DEFAULT_DURATION_SECONDS;
static uint32_t  sampleCount = 0;
static uint32_t  recordingStartMilliseconds = 0;
static bool      recording = false;

/* Ultimo valor recibido de cada canal, se grabe o no. Sirve para que la
   pagina web muestre el sensor en vivo antes de arrancar la ejecucion, que
   es como se comprueba que el cableado esta bien sin gastar una prueba. */
static float currentValues[static_cast<uint8_t>(LogChannel::COUNT)] = {};
static bool  currentValueIsValid[static_cast<uint8_t>(LogChannel::COUNT)] = {};

/**
 * @brief Arranca una grabacion nueva y descarta la anterior.
 *
 * @return void
 */
void loggerStart(void) {
  if (samples == nullptr) {
    samples = static_cast<LogSample *>(
        ps_malloc(MAXIMUM_SAMPLE_COUNT * sizeof(LogSample)));
  }
  if (samples == nullptr) {
    Serial.println("Error: no hay PSRAM para las muestras, no se graba");
    return;
  }

  sampleCount = 0;
  recordingStartMilliseconds = millis();
  recording = true;
}

/**
 * @brief Fija cuanto va a durar la proxima ejecucion.
 *
 * No afecta a una grabacion en curso. Un valor fuera de rango se
 * recorta al tope, y cero se ignora: siempre queda una duracion util.
 *
 * @param[in]  seconds  Duracion pedida, en segundos.
 *
 * @return void
 */
void loggerSetDurationSeconds(uint32_t seconds) {
  if (seconds == 0) {
    return;
  }
  if (seconds > MAXIMUM_DURATION_SECONDS) {
    seconds = MAXIMUM_DURATION_SECONDS;
  }
  durationSeconds = seconds;
}

/**
 * @brief Duracion vigente para una ejecucion.
 *
 * @return uint32_t  Segundos.
 */
uint32_t loggerGetDurationSeconds(void) {
  return durationSeconds;
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
  if (loggerGetElapsedMilliseconds() >= durationSeconds * 1000UL) {
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
    case LogChannel::WHEEL_RPM_FRONT_RIGHT:return "rpm_delantera_der";
    case LogChannel::WHEEL_RPM_REAR_LEFT:  return "rpm_trasera_izq";
    case LogChannel::WHEEL_RPM_REAR_RIGHT: return "rpm_trasera_der";
    case LogChannel::VEHICLE_SPEED_KPH:    return "velocidad_kmh";
    case LogChannel::DROPS_WHEEL_FRONT:    return "caidas_rueda_delantera";
    case LogChannel::DROPS_WHEEL_REAR:     return "caidas_rueda_trasera";
    case LogChannel::DROPS_TEMPERATURE:    return "caidas_temperatura";
    case LogChannel::COUNT:                break;
  }
  return "desconocido";
}
