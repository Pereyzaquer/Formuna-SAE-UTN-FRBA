/************************************************************
 *  Proyecto : ECU
 *  Archivo  : names.cpp
 *  Equipo   : UTN BA Motorsport Formula student team
 *  Fecha    : 20/9/2026
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Nombre legible de cada nodo del bus CAN, editable desde
 *  la pagina web y guardado en la flash del ESP32.
 *
 *  Hardware:
 *  --------------------------------------------------------
 *  - MCU: ESP32 S3. Los nombres van a la particion NVS, que
 *    sobrevive al reinicio y a la regrabacion por OTA.
 *
 *  Notas:
 *  --------------------------------------------------------
 *  El nombre es solo para mostrar: no cambia como se decodifica
 *  una trama ni las columnas del CSV, que quedan fijas para que
 *  el conversor a MoTeC no dependa de lo que alguien escribio en
 *  la pagina.
 *
 *  Un identificador que no tiene nombre guardado recibe el de
 *  fabrica de la tabla de abajo, y si tampoco esta ahi, queda
 *  "sin definir": es la forma en que se ve un nodo nuevo que
 *  aparecio en el bus y todavia nadie bautizo.
 *
 ************************************************************/

/************************************************************
 *                     INCLUDES
 ************************************************************/
#include "../include/ECU.h"
#include "../include/can_ids.h"
#include <Preferences.h>

/************************************************************
 *               CONSTANTES DEL SISTEMA
 ************************************************************/
/** Espacio de nombres en la NVS. Todos los nombres viven aca. */
static const char *PREFERENCES_NAMESPACE = "nombres";

/** Nombre de fabrica de cada identificador conocido. */
struct DefaultName {
  uint32_t    identifier;
  const char *name;
};

static const DefaultName DEFAULT_NAMES[] = {
    {CAN_ID_DRIVER,           "inversor"},
    {CAN_ID_BMS,              "bateria (BMS)"},
    {CAN_ID_APPS,             "acelerador (APPS)"},
    {CAN_ID_BSE,              "freno (BSE)"},
    {CAN_ID_RPM_FRONT,        "rueda delantera"},
    {CAN_ID_RPM_REAR,         "rueda trasera"},
    {CAN_ID_RPM_MOTOR,        "rpm motor"},
    {CAN_ID_STEERING,         "volante"},
    {CAN_ID_IMU,              "IMU"},
    {CAN_ID_TPMS,             "presion neumaticos"},
    {CAN_ID_TEST_TEMPERATURE, "temperatura"},
};
static const uint8_t DEFAULT_NAME_COUNT =
    sizeof(DEFAULT_NAMES) / sizeof(DEFAULT_NAMES[0]);

/************************************************************
 *                VARIABLES GLOBALES
 ************************************************************/
static Preferences preferences;

/* Buffer de salida de namesGet(): el nombre se copia aca y se devuelve
   un puntero. Lo lee una sola task (la web) asi que no hay carrera. */
static char nameBuffer[NAME_MAXIMUM_LENGTH + 1];

/************************************************************
 *                  FUNCIONES LOCALES
 ************************************************************/

/**
 * @brief Clave con la que se guarda un identificador en la NVS.
 *
 * La NVS acepta claves de hasta 15 caracteres; "id_0x200" entra.
 *
 * @param[in]   identifier  Identificador CAN.
 * @param[out]  key         Buffer de al menos 12 bytes.
 *
 * @return void
 */
static void buildKey(uint32_t identifier, char *key) {
  snprintf(key, 12, "id_%03lX", (unsigned long)identifier);
}

/************************************************************
 *                  FUNCIONES PUBLICAS
 ************************************************************/

/**
 * @brief Abre el almacenamiento de nombres. Llamar una vez al arrancar.
 *
 * @return void
 */
void namesInitialize(void) {
  preferences.begin(PREFERENCES_NAMESPACE, false);
}

/**
 * @brief Nombre legible de un identificador CAN.
 *
 * Primero el guardado por el usuario; si no hay, el de fabrica; si
 * tampoco, "sin definir".
 *
 * @param[in]  identifier  Identificador CAN.
 *
 * @return const char*  Nombre. Valido hasta la proxima llamada.
 */
const char *namesGet(uint32_t identifier) {
  char key[12];
  buildKey(identifier, key);

  size_t length = preferences.getString(key, nameBuffer, sizeof(nameBuffer));
  if (length > 0 && nameBuffer[0] != '\0') {
    return nameBuffer;
  }

  for (uint8_t index = 0; index < DEFAULT_NAME_COUNT; index++) {
    if (DEFAULT_NAMES[index].identifier == identifier) {
      return DEFAULT_NAMES[index].name;
    }
  }
  return "sin definir";
}

/**
 * @brief Guarda el nombre de un identificador en la flash.
 *
 * Un nombre vacio borra el guardado y vuelve al de fabrica.
 *
 * @param[in]  identifier  Identificador CAN.
 * @param[in]  name        Nombre nuevo; se recorta a NAME_MAXIMUM_LENGTH.
 *
 * @return void
 */
void namesSet(uint32_t identifier, const char *name) {
  char key[12];
  buildKey(identifier, key);

  if (name == nullptr || name[0] == '\0') {
    preferences.remove(key);
    return;
  }

  char trimmed[NAME_MAXIMUM_LENGTH + 1];
  strncpy(trimmed, name, NAME_MAXIMUM_LENGTH);
  trimmed[NAME_MAXIMUM_LENGTH] = '\0';
  preferences.putString(key, trimmed);
}
