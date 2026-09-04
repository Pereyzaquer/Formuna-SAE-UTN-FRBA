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
 *  can_ids.h de la ECU, que es la fuente unica de verdad. La
 *  copia que hay en este proyecto tiene que coincidir.
 *
 ************************************************************/
#pragma once

/************************************************************
 *                     INCLUDES
 ************************************************************/
#include <Arduino.h>
#include <driver/twai.h>

/************************************************************
 *                      PINES
 ************************************************************/
/* Definidos para la prueba de banco. Pendiente confirmarlos contra el
   cableado real de cada nodo. */
#define CAN_TRANSMIT_PIN   4  /**< Al pin TX del transceiver CAN. */
#define CAN_RECEIVE_PIN    5  /**< Al pin RX del transceiver CAN. */

#define DHT11_DATA_PIN     3  /**< Pin de datos del DHT11.        */

#define INFRARED_PULSE_PIN 8  /**< Salida D0 del modulo LM393.    */

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
 * @brief Inicializa el periferico TWAI a 500 kbps en modo normal.
 *
 * @return bool  true si el driver quedo instalado y arrancado.
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
 * @param[out]  data   Puntero a los dos bytes de destino.
 * @param[in]   value  Valor a guardar.
 *
 * @return void
 */
void writeInteger16LittleEndian(uint8_t *data, int16_t value);
