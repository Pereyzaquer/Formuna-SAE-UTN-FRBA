/************************************************************
 *  Proyecto : ECU
 *  Archivo  : speed.cpp
 *  Equipo   : UTN BA Motorsport Formula student team
 *  Fecha    : 10/9/2026
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Velocidad del vehiculo calculada a partir de las vueltas
 *  por minuto de las cuatro ruedas.
 *
 *  Hardware:
 *  --------------------------------------------------------
 *  - MCU: ESP32 S3.
 *  - Sensores: ninguno propio. Las RPM llegan por CAN desde
 *    los nodos de rueda.
 *
 *  Notas:
 *  --------------------------------------------------------
 *  Una rueda puede mentir por varios motivos: se bloquea al
 *  frenar, patina al acelerar, o su sensor se desajusta. Por eso
 *  la velocidad no es el promedio de las cuatro sin mas: se
 *  descarta la que se aparta del resto y se promedian las que
 *  quedan.
 *
 *  La referencia para decidir quien se aparta es la mediana y no
 *  el promedio. Una rueda muy equivocada arrastra el promedio
 *  hacia ella, y entonces la comparacion termina haciendose
 *  contra un valor que la rueda mala ya ensucio. La mediana no
 *  se mueve por un solo valor extremo.
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
 * Cuanto avanza el auto en una vuelta de rueda, en metros.
 *
 * TODO: falta el dato. Este valor corresponde a un neumatico de 18
 * pulgadas de diametro y es provisorio. Se mide sin calculos: se marca
 * la rueda y el piso, se empuja el auto una vuelta completa con el
 * neumatico a presion de pista y con el piloto arriba, y se mide la
 * distancia entre las dos marcas. Conviene medirlo asi y no calcularlo,
 * porque el neumatico se aplasta con el peso y avanza menos que su
 * circunferencia teorica.
 */
static const float WHEEL_TRAVEL_PER_TURN_METERS = 1.44f;

/**
 * Diferencia a partir de la cual una rueda se descarta del promedio.
 *
 * Criterio del equipo: 5 km/h respecto de la mediana de las ruedas que
 * estan reportando.
 */
static const float OUTLIER_TOLERANCE_KPH = 5.0f;

/**
 * Tiempo sin datos a partir del cual una rueda deja de contar.
 *
 * Los nodos de rueda emiten a 50 Hz, o sea cada 20 ms. Medio segundo
 * son veinticinco periodos perdidos: si no llego nada en ese lapso, esa
 * rueda no esta reportando.
 */
static const uint32_t WHEEL_TIMEOUT_MILLISECONDS = 500;

/** Cantidad de ruedas, para no repetir el cast en todo el archivo. */
static const uint8_t WHEEL_COUNT = static_cast<uint8_t>(WheelPosition::COUNT);

/************************************************************
 *                VARIABLES GLOBALES
 ************************************************************/
/* Las escribe taskCan y las leen taskState y el monitoreo web. */
static volatile float    wheelSpeedKph[WHEEL_COUNT] = {};
static volatile uint32_t wheelUpdateMilliseconds[WHEEL_COUNT] = {};
static volatile bool     anyWheelEverSeen = false;

/************************************************************
 *             PROTOTIPOS DE FUNCIONES LOCALES
 ************************************************************/
static uint8_t collectValidWheelSpeeds(float *speeds);
static float medianOf(float *speeds, uint8_t count);

/**
 * @brief Informa las vueltas por minuto medidas en una rueda.
 *
 * @param[in]  wheel                 Rueda que reporta.
 * @param[in]  revolutionsPerMinute  Vueltas por minuto medidas.
 *
 * @return void
 */
void speedUpdateWheel(WheelPosition wheel, uint16_t revolutionsPerMinute) {
  uint8_t index = static_cast<uint8_t>(wheel);
  if (index >= WHEEL_COUNT) {
    return;
  }

  /* Vueltas por minuto por metros de avance da metros por minuto. Por
     60 pasa a metros por hora y dividido 1000 a kilometros por hora, o
     sea multiplicar por 0.06 de una sola vez. */
  wheelSpeedKph[index] =
      revolutionsPerMinute * WHEEL_TRAVEL_PER_TURN_METERS * 0.06f;
  wheelUpdateMilliseconds[index] = millis();
  anyWheelEverSeen = true;
}

/**
 * @brief Devuelve la velocidad medida por una rueda.
 *
 * @param[in]   wheel  Rueda a consultar.
 * @param[out]  kph    Velocidad en kilometros por hora.
 *
 * @return bool  false si esa rueda no reporto hace demasiado tiempo.
 */
bool speedGetWheelKph(WheelPosition wheel, float *kph) {
  uint8_t index = static_cast<uint8_t>(wheel);
  if (index >= WHEEL_COUNT) {
    return false;
  }

  /* Nunca reporto: la marca de tiempo sigue en cero. */
  if (wheelUpdateMilliseconds[index] == 0) {
    return false;
  }

  if (millis() - wheelUpdateMilliseconds[index] > WHEEL_TIMEOUT_MILLISECONDS) {
    return false;
  }

  *kph = wheelSpeedKph[index];
  return true;
}

/**
 * @brief Cuantas ruedas tienen una medicion reciente.
 *
 * @return uint8_t  De 0 a 4.
 */
uint8_t speedGetValidWheelCount(void) {
  float ignorado;
  uint8_t count = 0;

  for (uint8_t index = 0; index < WHEEL_COUNT; index++) {
    if (speedGetWheelKph(static_cast<WheelPosition>(index), &ignorado)) {
      count++;
    }
  }

  return count;
}

/**
 * @brief Indica si alguna rueda reporto alguna vez desde el arranque.
 *
 * @return bool  true si alguna rueda hablo alguna vez.
 */
bool speedAnyWheelEverSeen(void) {
  return anyWheelEverSeen;
}

/**
 * @brief Calcula la velocidad del vehiculo a partir de las ruedas.
 *
 * @param[out]  kph  Velocidad en kilometros por hora.
 *
 * @return bool  false si no hay ninguna medicion confiable.
 */
bool speedGetVehicleKph(float *kph) {
  float speeds[WHEEL_COUNT];
  uint8_t validCount = collectValidWheelSpeeds(speeds);

  if (validCount == 0) {
    return false;
  }

  float reference = medianOf(speeds, validCount);

  float total = 0.0f;
  uint8_t used = 0;

  for (uint8_t index = 0; index < validCount; index++) {
    if (fabsf(speeds[index] - reference) <= OUTLIER_TOLERANCE_KPH) {
      total += speeds[index];
      used++;
    }
  }

  /* Puede pasar con dos ruedas que difieran mucho entre si: la mediana
     queda en el medio y las dos caen fuera de tolerancia. En ese caso no
     se inventa un numero, porque no hay forma de saber cual de las dos
     tiene razon. */
  if (used == 0) {
    return false;
  }

  *kph = total / used;
  return true;
}

/************************************************************
 *                  FUNCIONES LOCALES
 ************************************************************/

/**
 * @brief Junta en un arreglo las velocidades de las ruedas vigentes.
 *
 * @param[out]  speeds  Arreglo de al menos cuatro posiciones.
 *
 * @return uint8_t  Cuantas posiciones se llenaron.
 */
static uint8_t collectValidWheelSpeeds(float *speeds) {
  uint8_t count = 0;

  for (uint8_t index = 0; index < WHEEL_COUNT; index++) {
    float kph;
    if (speedGetWheelKph(static_cast<WheelPosition>(index), &kph)) {
      speeds[count] = kph;
      count++;
    }
  }

  return count;
}

/**
 * @brief Calcula la mediana de un arreglo, ordenandolo de paso.
 *
 * Se ordena por insercion porque son cuatro valores como maximo: para
 * esa cantidad es mas rapido y mucho mas facil de leer que cualquier
 * algoritmo elaborado.
 *
 * @param[in,out]  speeds  Valores a ordenar.
 * @param[in]      count   Cuantos valores hay.
 *
 * @return float  Mediana. Con cantidad par, el promedio de los dos
 *                valores del medio.
 */
static float medianOf(float *speeds, uint8_t count) {
  for (uint8_t i = 1; i < count; i++) {
    float value = speeds[i];
    int8_t j = i - 1;
    while (j >= 0 && speeds[j] > value) {
      speeds[j + 1] = speeds[j];
      j--;
    }
    speeds[j + 1] = value;
  }

  if (count % 2 == 1) {
    return speeds[count / 2];
  }

  return (speeds[count / 2 - 1] + speeds[count / 2]) / 2.0f;
}
