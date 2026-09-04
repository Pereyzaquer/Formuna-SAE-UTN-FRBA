/************************************************************
 *  Proyecto : SensorNode
 *  Archivo  : sensor_temperature.cpp
 *  Equipo   : UTN BA Motorsport Formula student team
 *  Fecha    : 2/9/2026
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Sensor de temperatura y humedad DHT11. Implementa la
 *  interfaz de sensor que declara SensorNode.h.
 *
 *  Hardware:
 *  --------------------------------------------------------
 *  - MCU: ESP32 C3.
 *  - Sensores: DHT11.
 *      DATA -> DHT11_DATA_PIN, con resistencia de 10 kohm a VCC
 *      VCC  -> 3.3 V
 *      GND  -> masa comun con la placa
 *
 *  Notas:
 *  --------------------------------------------------------
 *  El DHT11 funciona bien a 3.3 V, asi que no necesita
 *  conversor de nivel. Si el modulo que se use ya trae la
 *  resistencia de pull-up en la placa, no hay que agregar otra.
 *
 *  Es un sensor lento y de poca resolucion: entrega grados
 *  enteros, no decimales, y no admite mas de una lectura por
 *  segundo. Esas dos limitaciones son del componente y estan
 *  reflejadas en el periodo y en el comentario de la escala.
 *
 ************************************************************/

/************************************************************
 *                     INCLUDES
 ************************************************************/
#include "../include/SensorNode.h"
#include "can_ids.h"
#include <DHT.h>

/************************************************************
 *               CONSTANTES DEL SISTEMA
 ************************************************************/

/**
 * Una lectura por segundo.
 *
 * Es el limite del DHT11, no una decision nuestra: leerlo mas seguido
 * devuelve el valor viejo o directamente falla.
 */
static const uint32_t MEASUREMENT_PERIOD_MILLISECONDS = 1000;

/************************************************************
 *                VARIABLES GLOBALES
 ************************************************************/
static DHT sensor(DHT11_DATA_PIN, DHT11);

/**
 * @brief Prepara el sensor para medir.
 *
 * @return void
 */
void sensorInitialize(void) {
  sensor.begin();
}

/**
 * @brief Identificador CAN con el que este nodo publica sus datos.
 *
 * @return uint32_t  Identificador de 11 bits.
 */
uint32_t sensorGetCanIdentifier(void) {
  return CAN_ID_TEST_TEMPERATURE;
}

/**
 * @brief Cada cuanto tiene que medir y emitir este nodo.
 *
 * @return uint32_t  Periodo entre emisiones, en milisegundos.
 */
uint32_t sensorGetPeriodMilliseconds(void) {
  return MEASUREMENT_PERIOD_MILLISECONDS;
}

/**
 * @brief Mide temperatura y humedad, y arma la trama CAN.
 *
 * Layout, definido en can_ids.h:
 *   byte 0-1  temperatura, int16,  little-endian, 0.1 grados C/bit
 *   byte 2-3  humedad,     uint16, little-endian, 0.1 % HR/bit
 *
 * La escala de 0.1 permite un decimal aunque el DHT11 entregue grados
 * enteros. Se eligio asi para que un sensor mejor pueda reemplazarlo
 * sin tener que cambiar el layout ni el codigo de la ECU.
 *
 * @param[out]  data    Bytes de la trama.
 * @param[out]  length  Cantidad de bytes escritos.
 *
 * @return bool  false si la lectura fallo.
 */
bool sensorBuildFrame(uint8_t *data, uint8_t *length) {
  float temperatureCelsius = sensor.readTemperature();
  float humidityPercent    = sensor.readHumidity();

  /* La libreria devuelve "no es un numero" cuando la lectura falla, que
     pasa seguido si el cable es largo o falta la resistencia de pull-up.
     En ese caso no se emite nada y se reintenta al periodo siguiente. */
  if (isnan(temperatureCelsius) || isnan(humidityPercent)) {
    return false;
  }

  /* Se multiplica por diez porque la escala acordada es 0.1 por bit. */
  writeInteger16LittleEndian(&data[0],
                             static_cast<int16_t>(temperatureCelsius * 10.0f));
  writeInteger16LittleEndian(&data[2],
                             static_cast<int16_t>(humidityPercent * 10.0f));
  *length = 4;

  return true;
}
