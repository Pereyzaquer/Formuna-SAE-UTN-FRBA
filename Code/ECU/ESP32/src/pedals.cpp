/************************************************************
 *  Proyecto : ECU
 *  Archivo  : pedals.cpp
 *  Equipo   : UTN BA Motorsport Formula student team
 *  Fecha    : 23/10/2026
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Lectura del acelerador (APPS, dos potenciometros), del
 *  sensor de freno y del estado del shutdown circuit. Detecta
 *  las fallas de cada uno; la maquina de estados decide que
 *  hacer con ellas.
 *
 *  Hardware:
 *  --------------------------------------------------------
 *  - MCU: ESP32 S3, entradas del ADC1 (el ADC2 no se puede
 *    usar con el wifi encendido).
 *  - Sensores de 5 V, salida de 0.5 a 4.5 V. Cada uno entra por
 *    un divisor de 10 k en serie y 15 k a masa, que deja 4.5 V
 *    en 2.7 V: el S3 no tolera mas de 3.3 V en un pin.
 *  - Shutdown circuit: los 12 V mueven un rele; su contacto une
 *    el pin a masa. Contacto cerrado = circuito con tension = OK.
 *
 *  Notas:
 *  --------------------------------------------------------
 *  Cada sensor se calibra desde la pagina web: "reposo" y "a
 *  fondo" toman la tension del momento. Asi el mapeo a por
 *  ciento sirve tambien para un sensor invertido (que baja la
 *  tension al pisar), como suele ser el segundo del APPS.
 *
 *  Una tension fuera de la banda valida del sensor quiere decir
 *  cable cortado o en corto: eso es falla, no un pedal en 0 %.
 *
 ************************************************************/

/************************************************************
 *                     INCLUDES
 ************************************************************/
#include "../include/ECU.h"
#include "../include/rules.h"
#include <Preferences.h>

/************************************************************
 *               CONSTANTES DEL SISTEMA
 ************************************************************/
/** Fraccion de la tension del sensor que llega al pin: 15 k / (10 k + 15 k). */
static const float DIVIDER_RATIO = 0.6f;

/** Mapeo de fabrica: el rango de salida del sensor. Se recalibra desde la web. */
static const float DEFAULT_REST_VOLTS = 0.5f;
static const float DEFAULT_FULL_VOLTS = 4.5f;

/**
 * Banda valida del sensor. Por fuera, el cable esta cortado (cerca de
 * 0 V) o en corto con la alimentacion (cerca de 5 V). Los margenes de
 * 0.2 V respecto del rango del sensor los definí para la prueba; queda
 * pendiente validarlos con los sensores del auto.
 */
static const float MINIMUM_VALID_VOLTS = 0.3f;
static const float MAXIMUM_VALID_VOLTS = 4.7f;

/**
 * Cuanto tiene que sostenerse una lectura fuera de banda para ser falla.
 * Filtra un pico de ruido; usa el mismo tiempo que la regla del APPS.
 */
static const uint32_t OUT_OF_RANGE_MILLISECONDS = RULE_APPS_IMPLAUSIBILITY_TIME_MS;

/** El shutdown tiene que quedar estable este tiempo para creerle. */
static const uint32_t SHUTDOWN_DEBOUNCE_MILLISECONDS = 20;

/** Al registrador va una muestra de cada pedal cada este tiempo. */
static const uint32_t RECORD_PERIOD_MILLISECONDS = 50;

/**
 * Filtro de las lecturas del ADC: cada medicion pesa este tanto y el
 * resto lo aporta la anterior. Saca el ruido del ADC sin agregar mas de
 * unos 30 ms de demora con una lectura cada 10 ms.
 */
static const float FILTER_NEW_WEIGHT = 0.3f;

/************************************************************
 *                VARIABLES GLOBALES
 ************************************************************/
struct PedalSensor {
  uint8_t     pin;
  const char *name;         /**< Para la pagina web. */
  const char *restKey;      /**< Claves de la calibracion en la flash. */
  const char *fullKey;
  float       restVolts;
  float       fullVolts;
  float       volts;        /**< Tension del sensor, ya sin el divisor. */
  float       percent;
  bool        inRange;
  uint32_t    outOfRangeSinceMilliseconds;
};

/* Las escribe taskState (pedalsUpdate) y las leen la web y la pantalla:
   valores sueltos de 32 bits. */
static PedalSensor sensors[PEDAL_SENSOR_COUNT] = {
    {ACCELERATOR_1_PIN, "APPS 1", "a1_reposo", "a1_fondo", 0, 0, 0, 0, true, 0},
    {ACCELERATOR_2_PIN, "APPS 2", "a2_reposo", "a2_fondo", 0, 0, 0, 0, true, 0},
    {BRAKE_PIN,         "Freno",  "fr_reposo", "fr_fondo", 0, 0, 0, 0, true, 0},
};

static Preferences settings;

static volatile float    acceleratorPercent = 0.0f;
static volatile float    acceleratorDifferencePercent = 0.0f;
static volatile uint32_t acceleratorImplausibleSinceMilliseconds = 0;
static volatile bool     brakeAndAcceleratorConflict = false;
static volatile bool     shutdownCircuitOpen = true;

/************************************************************
 *                  FUNCIONES LOCALES
 ************************************************************/

/**
 * @brief Lee un sensor, lo filtra, lo pasa a por ciento y revisa su banda.
 *
 * @param[in,out]  sensor  Sensor a actualizar.
 *
 * @return void
 */
static void readSensor(PedalSensor *sensor) {
  float pinVolts = analogReadMilliVolts(sensor->pin) / 1000.0f;
  float volts = pinVolts / DIVIDER_RATIO;
  sensor->volts = sensor->volts + FILTER_NEW_WEIGHT * (volts - sensor->volts);

  float span = sensor->fullVolts - sensor->restVolts;
  float percent = (span != 0.0f) ? (sensor->volts - sensor->restVolts) / span * 100.0f : 0.0f;
  if (percent < 0.0f) {
    percent = 0.0f;
  }
  if (percent > 100.0f) {
    percent = 100.0f;
  }
  sensor->percent = percent;

  bool inBand = sensor->volts >= MINIMUM_VALID_VOLTS && sensor->volts <= MAXIMUM_VALID_VOLTS;
  if (inBand) {
    sensor->outOfRangeSinceMilliseconds = 0;
    sensor->inRange = true;
  } else {
    if (sensor->outOfRangeSinceMilliseconds == 0) {
      sensor->outOfRangeSinceMilliseconds = millis();
    }
    sensor->inRange = millis() - sensor->outOfRangeSinceMilliseconds < OUT_OF_RANGE_MILLISECONDS;
  }
}

/**
 * @brief Lee el shutdown circuit con antirrebote.
 *
 * @return void
 */
static void readShutdownCircuit(void) {
  static bool     lastReading = true;
  static uint32_t lastChangeMilliseconds = 0;

  bool open = (digitalRead(SHUTDOWN_CIRCUIT_PIN) == HIGH);
  if (open != lastReading) {
    lastReading = open;
    lastChangeMilliseconds = millis();
  }
  if (millis() - lastChangeMilliseconds >= SHUTDOWN_DEBOUNCE_MILLISECONDS) {
    shutdownCircuitOpen = open;
  }
}

/**
 * @brief Regla de las dos señales del APPS: tienen que coincidir.
 *
 * Si difieren mas de RULE_APPS_IMPLAUSIBILITY_PCT, se anota desde
 * cuando; pedalsAcceleratorFault() decide si ya duro lo suficiente.
 *
 * @return void
 */
static void checkAcceleratorAgreement(void) {
  PedalSensor *first = &sensors[ACCELERATOR_1];
  PedalSensor *second = &sensors[ACCELERATOR_2];

  float difference = fabsf(first->percent - second->percent);
  acceleratorDifferencePercent = difference;
  acceleratorPercent = (first->percent + second->percent) / 2.0f;

  if (difference > RULE_APPS_IMPLAUSIBILITY_PCT) {
    if (acceleratorImplausibleSinceMilliseconds == 0) {
      acceleratorImplausibleSinceMilliseconds = millis();
    }
  } else {
    acceleratorImplausibleSinceMilliseconds = 0;
  }
}

/**
 * @brief Regla freno y acelerador a la vez.
 *
 * Con el freno pisado y el acelerador por encima de
 * RULE_APPS_BRAKE_CUT_PCT se corta; se rearma recien cuando el
 * acelerador baja de RULE_APPS_REARM_PCT, aunque ya se haya soltado el
 * freno.
 *
 * @return void
 */
static void checkBrakeAndAccelerator(void) {
  bool brakePressed = sensors[BRAKE].percent > RULE_BRAKE_PRESSED_PCT;

  if (brakePressed && acceleratorPercent > RULE_APPS_BRAKE_CUT_PCT) {
    brakeAndAcceleratorConflict = true;
  }
  if (acceleratorPercent < RULE_APPS_REARM_PCT) {
    brakeAndAcceleratorConflict = false;
  }
}

/**
 * @brief Manda al registrador una muestra de cada pedal, con limite de ritmo.
 *
 * @return void
 */
static void recordValues(void) {
  static uint32_t lastRecordMilliseconds = 0;
  if (millis() - lastRecordMilliseconds < RECORD_PERIOD_MILLISECONDS) {
    return;
  }
  lastRecordMilliseconds = millis();

  loggerRecordValue(LogChannel::ACCELERATOR_PERCENT, acceleratorPercent);
  loggerRecordValue(LogChannel::ACCELERATOR_1_PERCENT, sensors[ACCELERATOR_1].percent);
  loggerRecordValue(LogChannel::ACCELERATOR_2_PERCENT, sensors[ACCELERATOR_2].percent);
  loggerRecordValue(LogChannel::BRAKE_PERCENT, sensors[BRAKE].percent);
}

/************************************************************
 *                  FUNCIONES PUBLICAS
 ************************************************************/

/**
 * @brief Prepara las entradas y carga la calibracion guardada.
 *
 * @return void
 */
void pedalsInitialize(void) {
  pinMode(SHUTDOWN_CIRCUIT_PIN, INPUT_PULLUP);
  settings.begin("pedales", false);

  for (uint8_t index = 0; index < PEDAL_SENSOR_COUNT; index++) {
    PedalSensor *sensor = &sensors[index];
    sensor->restVolts = settings.isKey(sensor->restKey) ? settings.getFloat(sensor->restKey)
                                                       : DEFAULT_REST_VOLTS;
    sensor->fullVolts = settings.isKey(sensor->fullKey) ? settings.getFloat(sensor->fullKey)
                                                       : DEFAULT_FULL_VOLTS;
    /* Primera lectura sin filtro, para no arrancar desde 0 V y marcar
       una falla que no existe. */
    sensor->volts = analogReadMilliVolts(sensor->pin) / 1000.0f / DIVIDER_RATIO;
  }
}

/**
 * @brief Lee todo y aplica las reglas. La llama taskState en cada vuelta.
 *
 * @return void
 */
void pedalsUpdate(void) {
  for (uint8_t index = 0; index < PEDAL_SENSOR_COUNT; index++) {
    readSensor(&sensors[index]);
  }
  readShutdownCircuit();
  checkAcceleratorAgreement();
  checkBrakeAndAccelerator();
  recordValues();
}

/**
 * @brief Fija el punto de reposo o el de a fondo con la tension actual.
 *
 * @param[in]  sensor  Cual de los tres sensores.
 * @param[in]  full    true para "a fondo", false para "reposo".
 *
 * @return void
 */
void pedalsCalibrate(PedalSensorIndex sensor, bool full) {
  if (sensor >= PEDAL_SENSOR_COUNT) {
    return;
  }
  PedalSensor *target = &sensors[sensor];
  if (full) {
    target->fullVolts = target->volts;
    settings.putFloat(target->fullKey, target->fullVolts);
  } else {
    target->restVolts = target->volts;
    settings.putFloat(target->restKey, target->restVolts);
  }
}

/**
 * @brief Datos de un sensor para mostrar en la web.
 *
 * @param[in]   sensor  Cual de los tres sensores.
 * @param[out]  status  Copia de su estado.
 *
 * @return void
 */
void pedalsGetSensorStatus(PedalSensorIndex sensor, PedalSensorStatus *status) {
  const PedalSensor *source = &sensors[sensor];
  status->name      = source->name;
  status->volts     = source->volts;
  status->percent   = source->percent;
  status->restVolts = source->restVolts;
  status->fullVolts = source->fullVolts;
  status->inRange   = source->inRange;
}

/**
 * @brief Acelerador, promedio de los dos potenciometros.
 *
 * @return float  De 0 a 100.
 */
float pedalsGetAcceleratorPercent(void) {
  return acceleratorPercent;
}

/**
 * @brief Diferencia entre las dos señales del APPS.
 *
 * @return float  En puntos de por ciento.
 */
float pedalsGetAcceleratorDifferencePercent(void) {
  return acceleratorDifferencePercent;
}

/**
 * @brief Freno, mapeado de 0 a 100 entre reposo y a fondo.
 *
 * @return float  De 0 a 100.
 */
float pedalsGetBrakePercent(void) {
  return sensors[BRAKE].percent;
}

/**
 * @brief Falla del APPS: algun potenciometro fuera de banda, o las dos
 *        señales en desacuerdo mas tiempo del que permite la regla.
 *
 * @return bool  true si hay que cortar.
 */
bool pedalsAcceleratorFault(void) {
  if (!sensors[ACCELERATOR_1].inRange || !sensors[ACCELERATOR_2].inRange) {
    return true;
  }
  uint32_t since = acceleratorImplausibleSinceMilliseconds;
  return since != 0 && millis() - since >= RULE_APPS_IMPLAUSIBILITY_TIME_MS;
}

/**
 * @brief Falla del sensor de freno: fuera de su banda valida.
 *
 * @return bool  true si el sensor esta cortado o en corto.
 */
bool pedalsBrakeFault(void) {
  return !sensors[BRAKE].inRange;
}

/**
 * @brief Freno y acelerador a la vez (ver checkBrakeAndAccelerator).
 *
 * @return bool  true mientras dure el corte.
 */
bool pedalsBrakeAndAcceleratorConflict(void) {
  return brakeAndAcceleratorConflict;
}

/**
 * @brief Estado del shutdown circuit.
 *
 * @return bool  true si esta abierto (sin los 12 V).
 */
bool pedalsShutdownCircuitOpen(void) {
  return shutdownCircuitOpen;
}
