/************************************************************
 *  Proyecto : SensorNode
 *  Archivo  : sensor_wheel_speed.cpp
 *  Equipo   : UTN BA Motorsport Formula student team
 *  Fecha    : 3/9/2026
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Sensor de velocidad de giro por deteccion de pulsos, con un
 *  modulo infrarrojo de obstaculos LM393. Implementa la
 *  interfaz de sensor que declara SensorNode.h.
 *
 *  Hardware:
 *  --------------------------------------------------------
 *  - MCU: ESP32 C3.
 *  - Sensores: modulo infrarrojo de obstaculos con LM393.
 *      D0   -> INFRARED_PULSE_PIN
 *      VCC  -> 3.3 V
 *      GND  -> masa comun con la placa
 *
 *  Notas:
 *  --------------------------------------------------------
 *  ALIMENTAR EL MODULO CON 3.3 V, NO CON 5 V. La salida del
 *  LM393 es colector abierto con resistencia de pull-up a su
 *  propia alimentacion: a 5 V entregaria 5 V, que el ESP32-C3
 *  no tolera en sus entradas.
 *
 *  El modulo no mide distancia: su salida es un bit que cambia
 *  cuando algo se acerca mas que el umbral del preset. Aca se
 *  usa esa transicion como pulso, contando cada objeto que pasa
 *  por delante. Con un disco con lengüetas girando, eso es una
 *  medicion de vueltas por minuto, igual que la rueda fonica
 *  del auto.
 *
 *  El preset del modulo hay que ajustarlo hasta que el led de
 *  la placa encienda con la lengüeta delante y se apague sin
 *  ella. Si queda muy sensible, dispara con cualquier cosa.
 *
 ************************************************************/

/************************************************************
 *                     INCLUDES
 ************************************************************/
#include "../include/SensorNode.h"
#include "can_ids.h"

/************************************************************
 *               CONSTANTES DEL SISTEMA
 ************************************************************/

/** Veinte emisiones por segundo, igual que el resto de la telemetria. */
static const uint32_t MEASUREMENT_PERIOD_MILLISECONDS = 50;

/**
 * Cuantos pulsos entrega el sensor en una vuelta completa.
 *
 * Definido en 1: una sola lengüeta en el disco, asi que cada pulso es
 * una vuelta. Si el disco tiene mas marcas hay que poner cuantas, o las
 * RPM van a salir multiplicadas por ese numero.
 */
static const uint32_t PULSES_PER_REVOLUTION = 1;

/**
 * Tiempo minimo entre dos pulsos para creerles.
 *
 * La salida del comparador no cambia limpio: en el borde de la
 * deteccion oscila y produce varios flancos por una sola lengüeta. Todo
 * pulso que llegue antes de este tiempo se descarta por ser rebote.
 *
 * Dos milisegundos permiten hasta 30000 RPM con una marca por vuelta,
 * de sobra para girar un disco a mano.
 */
static const uint32_t MINIMUM_PULSE_INTERVAL_MICROSECONDS = 2000;

/**
 * Tiempo sin pulsos a partir del cual se informa velocidad cero.
 *
 * Sin este limite, al frenar el disco el nodo seguiria informando para
 * siempre la ultima velocidad medida, porque nunca llegaria un pulso
 * nuevo que la corrija.
 *
 * Un segundo equivale a 60 RPM: por debajo de eso se lee como detenido.
 */
static const uint32_t STOPPED_TIMEOUT_MICROSECONDS = 1000000;

/************************************************************
 *                VARIABLES GLOBALES
 ************************************************************/
/* Las escribe la interrupcion y las lee el bucle principal, por eso son
   volatile: sin eso el compilador podria guardarlas en un registro y no
   ver nunca los cambios que hace la interrupcion. */
static volatile uint32_t lastPulseMicroseconds = 0;
static volatile uint32_t pulseIntervalMicroseconds = 0;

/************************************************************
 *             PROTOTIPOS DE FUNCIONES LOCALES
 ************************************************************/
static void IRAM_ATTR handlePulseInterrupt(void);

/**
 * @brief Prepara el sensor para medir.
 *
 * Se atiende por interrupcion y no leyendo el pin cada tanto, porque un
 * pulso puede durar menos que el intervalo entre lecturas y pasar
 * desapercibido.
 *
 * @return void
 */
void sensorInitialize(void) {
  pinMode(INFRARED_PULSE_PIN, INPUT);

  /* El modulo pone la salida en bajo cuando detecta un obstaculo, asi
     que el flanco que interesa es el de bajada. */
  attachInterrupt(digitalPinToInterrupt(INFRARED_PULSE_PIN),
                  handlePulseInterrupt, FALLING);
}

/**
 * @brief Identificador CAN con el que este nodo publica sus datos.
 *
 * Usa el identificador de RPM de ruedas delanteras del auto, no uno de
 * banco: medir vueltas por minuto contando pulsos es exactamente lo que
 * va a hacer el sensor definitivo, asi que este codigo no se tira.
 *
 * @return uint32_t  Identificador de 11 bits.
 */
uint32_t sensorGetCanIdentifier(void) {
  return CAN_ID_RPM_FRONT;
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
 * @brief Calcula las vueltas por minuto y arma la trama CAN.
 *
 * Layout, definido en can_ids.h:
 *   byte 0-1  RPM rueda izquierda, uint16, little-endian, 1 RPM/bit
 *   byte 2-3  RPM rueda derecha,   uint16, little-endian, 1 RPM/bit
 *
 * Este nodo tiene un solo sensor, asi que llena la rueda izquierda y
 * manda cero en la derecha. Se respeta el layout completo igual, para
 * no tener que cambiarlo cuando se sume el segundo sensor.
 *
 * @param[out]  data    Bytes de la trama.
 * @param[out]  length  Cantidad de bytes escritos.
 *
 * @return bool  Siempre true: detenido tambien es una medicion valida.
 */
bool sensorBuildFrame(uint8_t *data, uint8_t *length) {
  /* Se copian las dos variables de una sola vez con la interrupcion
     apagada, para que no cambien en la mitad de la lectura y quede un
     intervalo que corresponde a otro pulso. */
  noInterrupts();
  uint32_t interval = pulseIntervalMicroseconds;
  uint32_t lastPulse = lastPulseMicroseconds;
  interrupts();

  uint32_t revolutionsPerMinute = 0;
  bool     recentlyMoving = (micros() - lastPulse) < STOPPED_TIMEOUT_MICROSECONDS;

  if (recentlyMoving && interval > 0) {
    /* Un minuto son 60 millones de microsegundos. Dividido el tiempo
       entre pulsos da pulsos por minuto, y dividido las marcas del
       disco, vueltas por minuto. */
    revolutionsPerMinute = 60000000UL / (interval * PULSES_PER_REVOLUTION);
  }

  /* El layout reserva 16 bits por rueda: mas que eso no entra. */
  if (revolutionsPerMinute > 65535) {
    revolutionsPerMinute = 65535;
  }

  writeInteger16LittleEndian(&data[0], (int16_t)(uint16_t)revolutionsPerMinute);
  writeInteger16LittleEndian(&data[2], 0); /* Sin segundo sensor. */
  *length = 4;

  return true;
}

/************************************************************
 *                  FUNCIONES LOCALES
 ************************************************************/

/**
 * @brief Atiende cada pulso del sensor y mide el tiempo entre pulsos.
 *
 * Va en IRAM porque es una interrupcion: tiene que poder ejecutarse
 * aunque la memoria flash este ocupada. Por eso hace lo minimo posible
 * y no imprime ni calcula nada; la cuenta se hace en el bucle principal.
 *
 * @return void
 */
static void IRAM_ATTR handlePulseInterrupt(void) {
  uint32_t now = micros();
  uint32_t interval = now - lastPulseMicroseconds;

  /* Rebote del comparador: demasiado pronto para ser una marca nueva. */
  if (interval < MINIMUM_PULSE_INTERVAL_MICROSECONDS) {
    return;
  }

  pulseIntervalMicroseconds = interval;
  lastPulseMicroseconds = now;
}
