/************************************************************
 *  Proyecto : ECU
 *  Archivo  : display.cpp
 *  Equipo   : UTN BA Motorsport Formula student team
 *  Fecha    : 9/10/2026
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Pantalla del piloto: estado del auto, velocidad, barra de
 *  RPM, acelerador, freno y avisos.
 *
 *  Hardware:
 *  --------------------------------------------------------
 *  - MCU: ESP32 S3.
 *  - Pantalla TFT de 4" MSP4020, controlador ST7796S,
 *    480x320, sin touch. Pines y bus en ECU.h y platformio.ini.
 *
 *  Notas:
 *  --------------------------------------------------------
 *  La pantalla solo lee: no decide nada ni cambia ningun estado.
 *
 *  Las barras se actualizan 50 veces por segundo, para que se
 *  vea fluida la respuesta de un pedal. Para que eso no cueste,
 *  en cada vuelta se pinta solo la diferencia entre lo que habia
 *  y lo nuevo: si el acelerador paso de 40 % a 42 %, se pintan
 *  unas pocas lineas, no la barra entera. El resto de las zonas
 *  se repinta solo cuando su dato cambia.
 *
 *  Que canal alimenta la barra de RPM y las escalas se ajustan
 *  desde la pagina web y quedan guardados en la flash.
 *
 *  Modo prueba: la pagina puede mover las tres barras con
 *  deslizables, para ver la respuesta sin tener los sensores.
 *  Mientras dura, la pantalla muestra esos valores en lugar de los
 *  reales; no se graban en ningun lado. Si la pagina deja de mandar
 *  durante TEST_TIMEOUT_MILLISECONDS, se vuelve sola a los reales.
 *
 *    +------------------------------------------------+
 *    |              READY TO DRIVE (color)            |
 *    +----+-----------------------------------+-------+
 *    |    | [=========== RPM ======     ] 4520|       |
 *    | A  |                                   |   F   |
 *    | C  |               88                  |   R   |
 *    | E  | REC 34/90 s  km/h                 |   E   |
 *    | L  | [=====|=====|==   |     ]  62 %   |   N   |
 *    +----+-----------------------------------+-------+
 *    |                 AVISOS (color)                 |
 *    +------------------------------------------------+
 *
 ************************************************************/

/************************************************************
 *                     INCLUDES
 ************************************************************/
#include "../include/ECU.h"
#include "../include/state_font.h"
#include <TFT_eSPI.h>
#include <Preferences.h>

/************************************************************
 *               CONSTANTES DEL SISTEMA
 ************************************************************/
/** 50 veces por segundo: alcanza para que una barra se vea continua. */
static const uint32_t REFRESH_PERIOD_MILLISECONDS = 20;

/** Los textos cambian menos y se leen de reojo: 5 veces por segundo. */
static const uint32_t TEXT_REFRESH_PERIOD_MILLISECONDS = 200;

/** Sin noticias de la pagina durante este tiempo, termina el modo prueba. */
static const uint32_t TEST_TIMEOUT_MILLISECONDS = 2000;

/* Valores de fabrica de la configuracion. Son escalas de la pantalla,
   no limites del auto: se ajustan desde la web segun lo que se pruebe. */
static const LogChannel DEFAULT_REVOLUTIONS_CHANNEL   = LogChannel::WHEEL_RPM_FRONT_LEFT;
static const float      DEFAULT_REVOLUTIONS_FULL_SCALE = 1000.0f;
static const float      DEFAULT_BRAKE_FULL_SCALE_BAR   = 100.0f;

/* Geometria, en puntos. */
static const int16_t SCREEN_WIDTH   = 480;
static const int16_t HEADER_HEIGHT  = 56;
static const int16_t FOOTER_TOP     = 280;
static const int16_t FOOTER_HEIGHT  = 40;

static const int16_t PEDAL_TOP      = 72;
static const int16_t PEDAL_BOTTOM   = 252;
static const int16_t PEDAL_WIDTH    = 36;
static const int16_t ACCELERATOR_LEFT = 14;
static const int16_t BRAKE_LEFT     = SCREEN_WIDTH - 14 - PEDAL_WIDTH;

static const int16_t REVOLUTIONS_LEFT   = 70;
static const int16_t REVOLUTIONS_TOP    = 72;
static const int16_t REVOLUTIONS_WIDTH  = 270;
static const int16_t REVOLUTIONS_HEIGHT = 24;

/* Barra de bateria: la fila de abajo del centro, del mismo largo que la
   de RPM, con el porcentaje en numero a la derecha. */
static const int16_t BATTERY_LEFT      = REVOLUTIONS_LEFT;
static const int16_t BATTERY_TOP       = 234;
static const int16_t BATTERY_WIDTH     = 260;
static const int16_t BATTERY_HEIGHT    = 22;

/* "REC" va a la izquierda de "km/h", a la misma altura. */
static const int16_t RECORDING_LEFT    = REVOLUTIONS_LEFT;
static const int16_t RECORDING_MIDDLE  = 215;

/** Debajo de estos porcentajes la bateria se pinta ambar y roja. Son
    umbrales de la pantalla para llamar la atencion, no del pack. */
static const float BATTERY_WARNING_PERCENT = 30.0f;
static const float BATTERY_LOW_PERCENT     = 15.0f;

/** Las zonas de la barra de RPM, como fraccion del fondo de escala. */
static const float REVOLUTIONS_WARNING_FRACTION = 0.70f;
static const float REVOLUTIONS_LIMIT_FRACTION   = 0.90f;

/* Colores, en el formato de 16 bits de la pantalla. */
static const uint16_t COLOR_BACKGROUND  = 0x0841;  /* casi negro */
static const uint16_t COLOR_TRACK       = 0x2104;  /* gris oscuro, fondo de barra */
static const uint16_t COLOR_TEXT        = TFT_WHITE;
static const uint16_t COLOR_DIMMED      = 0x8C71;  /* gris */
static const uint16_t COLOR_GOOD        = 0x2E8B;  /* verde */
static const uint16_t COLOR_WARNING     = 0xFD20;  /* ambar */
static const uint16_t COLOR_BAD         = 0xE8E4;  /* rojo */
static const uint16_t COLOR_INFORMATION = 0x3C7F;  /* azul */

/************************************************************
 *                VARIABLES GLOBALES
 ************************************************************/
static TFT_eSPI    screen;
static Preferences settings;

/* Configuracion vigente. La escribe la task de wifi desde la pagina y
   la lee esta task; son valores sueltos de 32 bits. */
static volatile LogChannel revolutionsChannel   = DEFAULT_REVOLUTIONS_CHANNEL;
static volatile float      revolutionsFullScale = DEFAULT_REVOLUTIONS_FULL_SCALE;
static volatile float      brakeFullScaleBar    = DEFAULT_BRAKE_FULL_SCALE_BAR;

/* Modo prueba: valores que manda la pagina y cuando llego el ultimo. */
static volatile bool     testIsActive = false;
static volatile float    testAcceleratorPercent = 0.0f;
static volatile float    testBrakeBar = 0.0f;
static volatile float    testRevolutions = 0.0f;
static volatile uint32_t testLastMilliseconds = 0;

/************************************************************
 *                  FUNCIONES LOCALES
 ************************************************************/

/**
 * @brief Color de la franja superior para cada estado.
 *
 * Los mismos colores que el led RGB y la pagina web.
 *
 * @param[in]  state  Estado del auto.
 *
 * @return uint16_t  Color.
 */
static uint16_t colorForState(EcuState state) {
  switch (state) {
    case EcuState::FAULT:     return COLOR_BAD;
    case EcuState::BOOT:      return COLOR_INFORMATION;
    case EcuState::CONFIG:    return COLOR_WARNING;
    case EcuState::CAR_READY: return COLOR_GOOD;
    case EcuState::CAR_ON:    return COLOR_GOOD;
  }
  return COLOR_DIMMED;
}

/**
 * @brief Texto de la franja superior para cada estado.
 *
 * Para el piloto, CAR_READY se lee como "READY TO DRIVE": es lo que
 * tiene que hacer a continuacion.
 *
 * @param[in]  state  Estado del auto.
 *
 * @return const char*  Texto a mostrar.
 */
static const char *textForState(EcuState state) {
  if (state == EcuState::CAR_READY) {
    return "READY TO DRIVE";
  }
  return stateGetName(state);
}

/**
 * @brief Indica si un texto cambio desde la ultima vez, y lo recuerda.
 *
 * Repintar un numero grande cuesta; si no cambio, no se toca.
 *
 * @param[in]      text      Texto nuevo.
 * @param[in,out]  lastText  Ultimo texto dibujado en esa zona.
 * @param[in]      size      Tamano de lastText.
 *
 * @return bool  true si hay que redibujar.
 */
static bool textChanged(const char *text, char *lastText, size_t size) {
  if (strncmp(text, lastText, size) == 0) {
    return false;
  }
  strncpy(lastText, text, size - 1);
  lastText[size - 1] = '\0';
  return true;
}

/**
 * @brief Pasa un valor a una fraccion de 0 a 1 de su escala.
 *
 * @param[in]  value      Valor medido.
 * @param[in]  fullScale  Valor que llena la barra.
 *
 * @return float  Fraccion recortada a [0, 1].
 */
static float toFraction(float value, float fullScale) {
  if (fullScale <= 0.0f) {
    return 0.0f;
  }
  float fraction = value / fullScale;
  if (fraction < 0.0f) {
    return 0.0f;
  }
  if (fraction > 1.0f) {
    return 1.0f;
  }
  return fraction;
}

/**
 * @brief Carga la configuracion guardada en la flash.
 *
 * @return void
 */
static void loadSettings(void) {
  settings.begin("pantalla", false);

  uint8_t channel = settings.getUChar("rpm_canal",
                                      static_cast<uint8_t>(DEFAULT_REVOLUTIONS_CHANNEL));
  if (channel < static_cast<uint8_t>(LogChannel::COUNT)) {
    revolutionsChannel = static_cast<LogChannel>(channel);
  }
  /* Se pregunta antes de leer: leer una clave que no existe deja un
     mensaje de error en el puerto serie, aunque no pase nada. */
  if (settings.isKey("rpm_maximo")) {
    revolutionsFullScale = settings.getFloat("rpm_maximo");
  }
  if (settings.isKey("freno_maximo")) {
    brakeFullScaleBar = settings.getFloat("freno_maximo");
  }
}

/**
 * @brief Dibuja lo que no cambia nunca: fondo, rieles y rotulos.
 *
 * @return void
 */
static void drawFixedParts(void) {
  screen.fillScreen(COLOR_BACKGROUND);

  screen.fillRect(ACCELERATOR_LEFT, PEDAL_TOP, PEDAL_WIDTH, PEDAL_BOTTOM - PEDAL_TOP, COLOR_TRACK);
  screen.fillRect(BRAKE_LEFT, PEDAL_TOP, PEDAL_WIDTH, PEDAL_BOTTOM - PEDAL_TOP, COLOR_TRACK);
  screen.fillRect(REVOLUTIONS_LEFT, REVOLUTIONS_TOP, REVOLUTIONS_WIDTH, REVOLUTIONS_HEIGHT, COLOR_TRACK);

  screen.setTextColor(COLOR_DIMMED, COLOR_BACKGROUND);
  screen.setTextDatum(TC_DATUM);
  screen.drawString("ACEL", ACCELERATOR_LEFT + PEDAL_WIDTH / 2, PEDAL_BOTTOM + 6, 2);
  screen.drawString("FRENO", BRAKE_LEFT + PEDAL_WIDTH / 2, PEDAL_BOTTOM + 6, 2);
  screen.drawString("km/h", SCREEN_WIDTH / 2, 202, 4);
}

/**
 * @brief Franja superior: el estado sobre su color.
 *
 * @param[in]  state  Estado del auto.
 *
 * @return void
 */
static void drawHeader(EcuState state) {
  uint16_t color = colorForState(state);

  /* Fuente suavizada (state_font.h): cada borde lleva su nivel de gris
     y se mezcla con el color de la franja, por eso se ve nitida. Se carga
     solo para dibujar el estado; el resto de los textos usa las fuentes
     comunes de la libreria. */
  screen.fillRect(0, 0, SCREEN_WIDTH, HEADER_HEIGHT, color);
  screen.loadFont(STATE_FONT);
  screen.setTextColor(TFT_BLACK, color);
  screen.setTextDatum(MC_DATUM);
  screen.drawString(textForState(state), SCREEN_WIDTH / 2, HEADER_HEIGHT / 2);
  screen.unloadFont();
}

/**
 * @brief Pinta un tramo de una barra vertical, de abajo hacia arriba.
 *
 * Solo toca las lineas entre la altura anterior y la nueva: llenar si
 * subio, devolver al color del riel si bajo.
 *
 * @param[in]      left          Borde izquierdo de la barra.
 * @param[in]      color         Color del relleno.
 * @param[in]      newHeight     Altura nueva, en puntos.
 * @param[in,out]  drawnHeight   Altura que esta dibujada; se actualiza.
 *
 * @return void
 */
static void updateVerticalBar(int16_t left, uint16_t color, int16_t newHeight,
                              int16_t *drawnHeight) {
  if (newHeight > *drawnHeight) {
    screen.fillRect(left, PEDAL_BOTTOM - newHeight, PEDAL_WIDTH,
                    newHeight - *drawnHeight, color);
  } else if (newHeight < *drawnHeight) {
    screen.fillRect(left, PEDAL_BOTTOM - *drawnHeight, PEDAL_WIDTH,
                    *drawnHeight - newHeight, COLOR_TRACK);
  }
  *drawnHeight = newHeight;
}

/**
 * @brief Color de la barra de RPM en un punto, segun su zona.
 *
 * @param[in]  offset  Distancia desde el borde izquierdo de la barra.
 *
 * @return uint16_t  Verde, ambar o rojo.
 */
static uint16_t revolutionsColorAt(int16_t offset) {
  if (offset >= REVOLUTIONS_WIDTH * REVOLUTIONS_LIMIT_FRACTION) {
    return COLOR_BAD;
  }
  if (offset >= REVOLUTIONS_WIDTH * REVOLUTIONS_WARNING_FRACTION) {
    return COLOR_WARNING;
  }
  return COLOR_GOOD;
}

/**
 * @brief Pinta un tramo de la barra de RPM, respetando el color de cada zona.
 *
 * @param[in]  from  Inicio del tramo, desde el borde izquierdo.
 * @param[in]  to    Fin del tramo (sin incluir).
 * @param[in]  fill  true para rellenar, false para devolver al riel.
 *
 * @return void
 */
static void paintRevolutionsSpan(int16_t from, int16_t to, bool fill) {
  while (from < to) {
    uint16_t color = fill ? revolutionsColorAt(from) : COLOR_TRACK;
    int16_t end = from + 1;
    while (end < to && (!fill || revolutionsColorAt(end) == color)) {
      end++;
    }
    screen.fillRect(REVOLUTIONS_LEFT + from, REVOLUTIONS_TOP, end - from,
                    REVOLUTIONS_HEIGHT, color);
    from = end;
  }
}

/**
 * @brief Barra horizontal de RPM: pinta solo lo que cambio.
 *
 * @param[in]      newWidth     Ancho nuevo del relleno, en puntos.
 * @param[in,out]  drawnWidth   Ancho dibujado; se actualiza.
 *
 * @return void
 */
static void updateRevolutionsBar(int16_t newWidth, int16_t *drawnWidth) {
  if (newWidth > *drawnWidth) {
    paintRevolutionsSpan(*drawnWidth, newWidth, true);
  } else if (newWidth < *drawnWidth) {
    paintRevolutionsSpan(newWidth, *drawnWidth, false);
  }
  *drawnWidth = newWidth;
}

/**
 * @brief Numero de RPM a la derecha de la barra.
 *
 * @param[in]  valid  true si el canal tiene dato.
 * @param[in]  value  Valor del canal.
 *
 * @return void
 */
static void drawRevolutionsNumber(bool valid, float value) {
  static char lastText[12] = "";
  char text[12];
  if (valid) {
    snprintf(text, sizeof(text), "%.0f", value);
  } else {
    snprintf(text, sizeof(text), "--");
  }
  if (!textChanged(text, lastText, sizeof(lastText))) {
    return;
  }

  screen.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
  screen.setTextDatum(MR_DATUM);
  screen.setTextPadding(60);
  screen.drawString(text, BRAKE_LEFT - 10, REVOLUTIONS_TOP + REVOLUTIONS_HEIGHT / 2, 4);
  screen.setTextPadding(0);
}

/**
 * @brief Velocidad del auto, en numeros grandes, sin decimales.
 *
 * Sin dato confiable se muestran guiones: un cero seria mentir.
 *
 * @return void
 */
static void drawSpeed(void) {
  float kph = 0.0f;
  char text[8];

  static char lastText[8] = "";

  if (speedGetVehicleKph(&kph)) {
    snprintf(text, sizeof(text), "%.0f", kph);
  } else {
    snprintf(text, sizeof(text), "--");
  }
  if (!textChanged(text, lastText, sizeof(lastText))) {
    return;
  }

  screen.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
  screen.setTextDatum(MC_DATUM);
  screen.setTextSize(2);
  screen.setTextPadding(220);
  screen.drawString(text, SCREEN_WIDTH / 2, 150, 7);
  screen.setTextPadding(0);
  screen.setTextSize(1);
}

/**
 * @brief Barra de bateria con el estado de carga que informa el BMS.
 *
 * Lleva cortes en 25, 50 y 75 % para leerla de un vistazo, y el
 * porcentaje en numero a la derecha. Sin dato reciente del BMS queda
 * vacia, con guiones: no se muestra un valor viejo como si fuera actual.
 * Se repinta entera solo cuando cambia el numero.
 *
 * @return void
 */
static void drawBattery(void) {
  static char lastText[8] = "x";
  float percent = 0.0f;
  bool valid = canGetBatteryPercent(&percent);
  char text[8];

  if (valid) {
    snprintf(text, sizeof(text), "%.0f %%", percent);
  } else {
    snprintf(text, sizeof(text), "-- %%");
  }
  if (!textChanged(text, lastText, sizeof(lastText))) {
    return;
  }

  uint16_t color = COLOR_GOOD;
  if (percent < BATTERY_WARNING_PERCENT) {
    color = COLOR_WARNING;
  }
  if (percent < BATTERY_LOW_PERCENT) {
    color = COLOR_BAD;
  }

  int16_t filled = valid ? BATTERY_WIDTH * toFraction(percent, 100.0f) : 0;
  screen.fillRect(BATTERY_LEFT, BATTERY_TOP, filled, BATTERY_HEIGHT, color);
  screen.fillRect(BATTERY_LEFT + filled, BATTERY_TOP, BATTERY_WIDTH - filled,
                  BATTERY_HEIGHT, COLOR_TRACK);

  /* Cortes en 25, 50 y 75 %: dos puntos de fondo que parten la barra. */
  for (uint8_t quarter = 1; quarter <= 3; quarter++) {
    screen.fillRect(BATTERY_LEFT + BATTERY_WIDTH * quarter / 4 - 1, BATTERY_TOP, 2,
                    BATTERY_HEIGHT, COLOR_BACKGROUND);
  }
  /* Borne de la pila, en la punta. */
  screen.fillRect(BATTERY_LEFT + BATTERY_WIDTH + 2, BATTERY_TOP + 6, 4, BATTERY_HEIGHT - 12,
                  valid ? color : COLOR_TRACK);

  screen.setTextColor(valid ? COLOR_TEXT : COLOR_DIMMED, COLOR_BACKGROUND);
  screen.setTextDatum(ML_DATUM);
  screen.setTextPadding(BRAKE_LEFT - (BATTERY_LEFT + BATTERY_WIDTH + 14) - 4);
  screen.drawString(text, BATTERY_LEFT + BATTERY_WIDTH + 14, BATTERY_TOP + BATTERY_HEIGHT / 2, 4);
  screen.setTextPadding(0);
}

/**
 * @brief Indicador chico de grabacion, solo mientras graba.
 *
 * @return void
 */
static void drawRecording(void) {
  static char lastText[24] = "x";
  char text[24] = "";

  if (loggerIsRecording()) {
    snprintf(text, sizeof(text), "REC %lu/%lu s",
             (unsigned long)(loggerGetElapsedMilliseconds() / 1000),
             (unsigned long)loggerGetDurationSeconds());
  }
  if (!textChanged(text, lastText, sizeof(lastText))) {
    return;
  }

  screen.setTextColor(COLOR_BAD, COLOR_BACKGROUND);
  screen.setTextDatum(ML_DATUM);
  screen.setTextPadding(130);
  screen.drawString(text, RECORDING_LEFT, RECORDING_MIDDLE, 2);
  screen.setTextPadding(0);
}

/**
 * @brief Franja inferior: la falla o aviso vigente, sobre su color.
 *
 * Si el aviso es por un nodo callado, dice cual.
 *
 * @param[in]  level  Nivel de falla vigente.
 * @param[in]  code   Codigo de falla vigente.
 *
 * @return void
 */
static void drawWarnings(ErrorLevel level, uint16_t code) {
  uint16_t color = COLOR_GOOD;
  char text[64];

  if (level == ErrorLevel::NONE) {
    snprintf(text, sizeof(text), "SIN AVISOS");
  } else {
    color = (level == ErrorLevel::CRITICAL) ? COLOR_BAD : COLOR_WARNING;

    /* Para un nodo callado se dice cual, para no tener que buscarlo. */
    uint32_t silentIdentifier = sensorsGetSilentNode();
    if (code == ERROR_CODE_NODE_LOST && silentIdentifier != 0) {
      char name[NAME_MAXIMUM_LENGTH + 1];
      namesGet(silentIdentifier, name, sizeof(name));
      snprintf(text, sizeof(text), "%s: %s", errorGetDescription(code), name);
    } else {
      snprintf(text, sizeof(text), "%s", errorGetDescription(code));
    }
    /* En mayusculas: la franja se lee de reojo. */
    for (char *letter = text; *letter != '\0'; letter++) {
      *letter = toupper(*letter);
    }
  }

  screen.fillRect(0, FOOTER_TOP, SCREEN_WIDTH, FOOTER_HEIGHT, color);
  screen.setTextColor(TFT_BLACK, color);
  screen.setTextDatum(MC_DATUM);
  screen.drawString(text, SCREEN_WIDTH / 2, FOOTER_TOP + FOOTER_HEIGHT / 2, 4);
}

/************************************************************
 *                  FUNCIONES PUBLICAS
 ************************************************************/

/**
 * @brief Elige que canal alimenta la barra de RPM y su fondo de escala.
 *
 * Se aplica en la proxima vuelta de la pantalla y queda guardado.
 *
 * @param[in]  channel    Canal del registrador.
 * @param[in]  fullScale  Valor que llena la barra; debe ser positivo.
 *
 * @return void
 */
void displaySetRevolutionsBar(LogChannel channel, float fullScale) {
  if (static_cast<uint8_t>(channel) >= static_cast<uint8_t>(LogChannel::COUNT) ||
      fullScale <= 0.0f) {
    return;
  }
  revolutionsChannel   = channel;
  revolutionsFullScale = fullScale;
  settings.putUChar("rpm_canal", static_cast<uint8_t>(channel));
  settings.putFloat("rpm_maximo", fullScale);
}

/**
 * @brief Fija la presion de freno que llena la barra de freno.
 *
 * @param[in]  fullScaleBar  Presion en bar; debe ser positiva.
 *
 * @return void
 */
void displaySetBrakeFullScale(float fullScaleBar) {
  if (fullScaleBar <= 0.0f) {
    return;
  }
  brakeFullScaleBar = fullScaleBar;
  settings.putFloat("freno_maximo", fullScaleBar);
}

/**
 * @brief Muestra en las barras los valores de los deslizables de la web.
 *
 * Hay que seguir llamandola mientras dure la prueba: si pasan
 * TEST_TIMEOUT_MILLISECONDS sin una llamada, la pantalla vuelve sola a
 * los datos reales.
 *
 * @param[in]  acceleratorPercent  Acelerador, de 0 a 100.
 * @param[in]  brakeBar            Freno, en bar.
 * @param[in]  revolutions         Valor para la barra de RPM.
 *
 * @return void
 */
void displaySetTestValues(float acceleratorPercent, float brakeBar, float revolutions) {
  testAcceleratorPercent = acceleratorPercent;
  testBrakeBar           = brakeBar;
  testRevolutions        = revolutions;
  testLastMilliseconds   = millis();
  testIsActive           = true;
}

/**
 * @brief Termina el modo prueba: las barras vuelven a los datos reales.
 *
 * @return void
 */
void displayStopTest(void) {
  testIsActive = false;
}

/**
 * @brief Canal que alimenta la barra de RPM.
 *
 * @return LogChannel  Canal vigente.
 */
LogChannel displayGetRevolutionsChannel(void) {
  return revolutionsChannel;
}

/**
 * @brief Valor que llena la barra de RPM.
 *
 * @return float  Fondo de escala vigente.
 */
float displayGetRevolutionsFullScale(void) {
  return revolutionsFullScale;
}

/**
 * @brief Presion de freno que llena la barra de freno.
 *
 * @return float  Fondo de escala vigente, en bar.
 */
float displayGetBrakeFullScale(void) {
  return brakeFullScaleBar;
}

/**
 * @brief Task de la pantalla.
 *
 * Cada vuelta actualiza las tres barras (solo la diferencia). Los textos
 * se revisan cada TEXT_REFRESH_PERIOD_MILLISECONDS, y las franjas de
 * estado y avisos solo se repintan cuando cambian.
 *
 * @param[in]  argument  No se usa. Lo exige la firma de FreeRTOS.
 *
 * @return void
 */
void taskDisplay(void *argument) {
  (void)argument;

  loadSettings();

  screen.init();
  screen.setRotation(1);
  drawFixedParts();

  bool       firstPass = true;
  EcuState   lastState = EcuState::BOOT;
  ErrorLevel lastLevel = ErrorLevel::NONE;
  uint16_t   lastCode = 0;
  uint32_t   lastSilentNode = 0;
  uint32_t   lastTextMilliseconds = 0;

  int16_t acceleratorHeight = 0;
  int16_t brakeHeight = 0;
  int16_t revolutionsWidth = 0;

  const int16_t pedalRange = PEDAL_BOTTOM - PEDAL_TOP;

  for (;;) {
    float accelerator = 0.0f;
    float brake = 0.0f;
    float revolutions = 0.0f;

    bool acceleratorValid = false;
    bool brakeValid = false;
    bool revolutionsValid = false;

    if (testIsActive && millis() - testLastMilliseconds > TEST_TIMEOUT_MILLISECONDS) {
      testIsActive = false;
    }

    if (testIsActive) {
      accelerator = testAcceleratorPercent;
      brake       = testBrakeBar;
      revolutions = testRevolutions;
      acceleratorValid = brakeValid = revolutionsValid = true;
    } else {
      acceleratorValid = loggerGetCurrentValue(LogChannel::ACCELERATOR_PERCENT, &accelerator);
      brakeValid       = loggerGetCurrentValue(LogChannel::BRAKE_PRESSURE_BAR, &brake);
      revolutionsValid = loggerGetCurrentValue(revolutionsChannel, &revolutions);
    }

    updateVerticalBar(ACCELERATOR_LEFT, COLOR_GOOD,
                      acceleratorValid ? pedalRange * toFraction(accelerator, 100.0f) : 0,
                      &acceleratorHeight);
    updateVerticalBar(BRAKE_LEFT, COLOR_BAD,
                      brakeValid ? pedalRange * toFraction(brake, brakeFullScaleBar) : 0,
                      &brakeHeight);
    updateRevolutionsBar(revolutionsValid
                             ? REVOLUTIONS_WIDTH * toFraction(revolutions, revolutionsFullScale)
                             : 0,
                         &revolutionsWidth);

    if (firstPass || millis() - lastTextMilliseconds >= TEXT_REFRESH_PERIOD_MILLISECONDS) {
      EcuState   state = stateGet();
      ErrorLevel level = errorGetLevel();
      uint16_t   code  = errorGetCode();
      uint32_t   silentNode = sensorsGetSilentNode();

      if (firstPass || state != lastState) {
        drawHeader(state);
        lastState = state;
      }
      if (firstPass || level != lastLevel || code != lastCode || silentNode != lastSilentNode) {
        drawWarnings(level, code);
        lastLevel = level;
        lastCode = code;
        lastSilentNode = silentNode;
      }

      drawSpeed();
      drawRevolutionsNumber(revolutionsValid, revolutions);
      drawRecording();
      drawBattery();
      lastTextMilliseconds = millis();
    }

    firstPass = false;
    vTaskDelay(pdMS_TO_TICKS(REFRESH_PERIOD_MILLISECONDS));
  }
}
