/************************************************************
 *  Proyecto : SensorNode
 *  Archivo  : SensorNode.h
 *  Equipo   : UTN BA Motorsport Formula student team
 *  Fecha    : 2/9/2026
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Encabezado principal del nodo sensor. Define el contrato
 *  que tiene que cumplir cualquier sensor para colgarse del
 *  bus CAN del auto.
 *
 *  Notas:
 *  --------------------------------------------------------
 *  Un nodo hace siempre lo mismo: arranca el sensor, arranca el
 *  bus, y cada tanto arma una trama y la manda. Lo unico que
 *  cambia de un sensor a otro son las cuatro funciones del
 *  bloque "INTERFAZ DEL SENSOR", que implementa cada archivo
 *  sensor_*.cpp. main.cpp no sabe que sensor tiene conectado.
 *
 *  Los identificadores y el layout de cada trama estan en el
 *  can_ids.h de la ECU, que es la fuente unica de verdad: este
 *  proyecto lo incluye desde ahi, no tiene copia.
 *
 *  El nodo corre en tres placas distintas, y el entorno de
 *  platformio.ini elige cual. Todas hablan CAN por un modulo
 *  MCP2515 externo (can_mcp2515.cpp); el ESP32-C3 tambien puede
 *  usar su controlador interno (can_twai.cpp) si algun dia se
 *  le pone un transceiver.
 *
 *    Arduino Nano       5 V    sensor inductivo LJ12A3
 *    ESP32-C3           3.3 V  sensor infrarrojo LM393
 *    NodeMCU (ESP8266)  3.3 V  DHT11
 *
 *  Los archivos de sensor no saben en que placa estan: usan los
 *  nombres de pin de aca abajo y las dos funciones can*.
 *
 ************************************************************/
#pragma once

/************************************************************
 *                     INCLUDES
 ************************************************************/
#include <Arduino.h>

/************************************************************
 *                      PINES
 ************************************************************/
/* Cada placa tiene sus pines. Es el unico lugar del proyecto donde se
   distingue una de otra; el resto del codigo usa estos nombres.

   El SPI hacia el MCP2515 usa los pines fijos de cada placa (los pone
   la libreria sola); lo unico que se elige es el chip select. */

#if defined(ARDUINO_ARCH_AVR)
/* ---- Arduino Nano ------------------------------------------------ */
/* SPI fijo: D13 SCK, D11 MOSI, D12 MISO. Todo a 5 V, sin conversor. */
#define CAN_SPI_CHIP_SELECT_PIN 10 /**< CS del modulo MCP2515.           */
#define CAN_SPI_CLOCK_HZ        4000000UL /**< Sin conversor, puede ir rapido. */

#define DHT11_DATA_PIN          4  /**< Pin de datos del DHT11.           */

/* Tiene que ser D2 o D3: son los unicos pines del Nano que atienden
   interrupciones externas, y los pulsos se cuentan por interrupcion. */
#define WHEEL_PULSE_PIN         3  /**< Pulsos del sensor.                */

#elif defined(ARDUINO_ARCH_ESP8266)
/* ---- NodeMCU V3 LoLin (ESP8266) ----------------------------------- */
/* SPI fijo: D5 SCK, D7 MOSI, D6 MISO. La placa es de 3.3 V y el modulo
   de 5 V: la linea MISO (SO del modulo) vuelve con 5 V y pasa por un
   divisor resistivo antes de entrar. Ver ARQUITECTURA.md. */
#define CAN_SPI_CHIP_SELECT_PIN 4  /**< D2. No usar D8: es pin de boot.  */
#define CAN_SPI_CLOCK_HZ        1000000UL /**< Lento, por el conversor.     */

#define DHT11_DATA_PIN          5  /**< D1, datos del DHT11.              */

#define WHEEL_PULSE_PIN         12 /**< D6. Sin uso en esta placa.        */

#else
/* ---- ESP32-C3 Super Mini ------------------------------------------ */
/* SPI fijo del C3: GPIO 4 SCK, GPIO 6 MOSI, GPIO 5 MISO. La placa es de
   3.3 V y el modulo de 5 V: las cuatro lineas del SPI pasan por un
   conversor de nivel. Ver ARQUITECTURA.md. */
#define CAN_SPI_CHIP_SELECT_PIN 7  /**< CS del modulo MCP2515.           */
#define CAN_SPI_CLOCK_HZ        1000000UL /**< Lento, por el divisor.       */

/* Solo para can_twai.cpp, cuando haya transceiver. Comparten numero con
   el SPI de arriba, pero nunca se compilan los dos backends a la vez. */
#define CAN_TRANSMIT_PIN        4  /**< Al pin TX del transceiver CAN.    */
#define CAN_RECEIVE_PIN         5  /**< Al pin RX del transceiver CAN.    */

#define DHT11_DATA_PIN          3  /**< Pin de datos del DHT11.           */

#define WHEEL_PULSE_PIN         8  /**< D0 del modulo infrarrojo.         */

#endif

/* Las rutinas de interrupcion del ESP32 tienen que vivir en RAM y se
   marcan con IRAM_ATTR. En las otras placas la palabra no existe o no
   hace falta: se define vacia para que el mismo archivo de sensor
   compile en todas. */
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

/* Un nodo de rueda puede ser el delantero o el trasero. Lo decide el
   entorno de platformio.ini con -D WHEEL_CAN_IDENTIFIER=...; si no dice
   nada, es el delantero. */
#ifndef WHEEL_CAN_IDENTIFIER
#define WHEEL_CAN_IDENTIFIER CAN_ID_RPM_FRONT
#endif

/************************************************************
 *                 INTERFAZ DEL SENSOR
 ************************************************************/
/*
 * Estas cuatro funciones son todo lo que hay que escribir para sumar
 * un sensor nuevo. Las implementa un unico archivo sensor_*.cpp, y el
 * entorno de platformio.ini decide cual se compila.
 */

/**
 * @brief Prepara el sensor para medir.
 *
 * Se llama una sola vez, al arrancar.
 *
 * @return void
 */
void sensorInitialize(void);

/**
 * @brief Identificador CAN con el que este nodo publica sus datos.
 *
 * @return uint32_t  Identificador de 11 bits, tomado del mapa comun.
 */
uint32_t sensorGetCanIdentifier(void);

/**
 * @brief Cada cuanto tiene que medir y emitir este nodo.
 *
 * Lo impone el sensor, no la red: un DHT11 no admite mas de una
 * lectura por segundo por mas que el bus aguante mucho mas.
 *
 * @return uint32_t  Periodo entre emisiones, en milisegundos.
 */
uint32_t sensorGetPeriodMilliseconds(void);

/**
 * @brief Mide y arma la trama CAN con el resultado.
 *
 * @param[out]  data    Bytes de la trama, como maximo 8.
 * @param[out]  length  Cantidad de bytes escritos en data.
 *
 * @return bool  false si la medicion fallo y no hay que emitir nada.
 */
bool sensorBuildFrame(uint8_t *data, uint8_t *length);

/************************************************************
 *                   BUS CAN
 ************************************************************/

/**
 * @brief Deja el controlador CAN andando a 500 kbps.
 *
 * @return bool  true si quedo listo para transmitir.
 */
bool canInitialize(void);

/**
 * @brief Transmite una trama estandar por el bus.
 *
 * @param[in]  identifier  Identificador CAN de 11 bits.
 * @param[in]  data        Puntero a los bytes a enviar.
 * @param[in]  length      Cantidad de bytes, de 0 a 8.
 *
 * @return bool  true si la trama entro en la cola de transmision.
 */
bool canSendFrame(uint32_t identifier, const uint8_t *data, uint8_t length);

/************************************************************
 *              AYUDA PARA ARMAR TRAMAS
 ************************************************************/

/**
 * @brief Guarda un entero de 16 bits en dos bytes, little-endian.
 *
 * Little-endian quiere decir que el byte menos significativo va
 * primero: el numero 300, o sea 0x012C, se guarda como 0x2C 0x01.
 * La ECU lo lee con el mismo criterio; si uno de los dos lo hiciera al
 * reves, el numero llegaria cambiado sin que nada avise.
 *
 * Va en el encabezado, y no en un .cpp, para que la usen los dos
 * backends de CAN y los sensores sin depender de ningun archivo mas.
 *
 * @param[out]  data   Puntero a los dos bytes de destino.
 * @param[in]   value  Valor a guardar.
 *
 * @return void
 */
static inline void writeInteger16LittleEndian(uint8_t *data, int16_t value) {
  data[0] = static_cast<uint8_t>(value & 0xFF);
  data[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}
