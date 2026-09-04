/************************************************************
 *  Proyecto : ECU
 *  Archivo  : can_ids.h
 *  Equipo   : UTN BA Motorsport Formula student team
 *  Fecha    : 14/8/2026
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Mapa de identificadores del bus CAN del vehiculo.
 *  Fuente unica de verdad: ningun otro archivo define IDs.
 *
 *  Notas:
 *  --------------------------------------------------------
 *  Los IDs mas bajos ganan el arbitraje del bus, por eso la
 *  emergencia esta en 0x080 y la telemetria lenta mas alta.
 *
 *  Cada trama necesita cuatro acuerdos entre el que emite y el
 *  que recibe, porque por el bus viajan bytes crudos y nada mas:
 *
 *    UNIDAD      que magnitud representa el numero
 *    ESCALA      cuantas unidades vale un bit
 *    RANGO       que valores entran en los bytes elegidos
 *    ENDIANNESS  que byte va primero
 *
 *  Si emisor y receptor no coinciden en los cuatro, el dato no
 *  falla: miente en silencio. Por eso estan escritos aca.
 *
 ************************************************************/
#pragma once

#include <stdint.h>

/************************************************************
 *          NODOS DEL AUTO (layout todavia abierto)
 ************************************************************/

/**
 * @brief Falla / emergencia. Asincrono.
 *
 * ID mas bajo del mapa para que gane el arbitraje contra todo lo demas.
 *
 * Layout provisorio, definido para la prueba de banco:
 *   byte 0    nivel de falla (ErrorLevel), uint8
 *   byte 1-2  codigo de falla, uint16, big-endian
 *
 * Pendiente para el auto real: agregar el nodo que origina la
 * falla y acordar la tabla de codigos completa.
 */
constexpr uint32_t CAN_ID_FAULT = 0x080;

/**
 * @brief Estado de la ECU + heartbeat. 100 Hz.
 *
 * Layout provisorio, definido para la prueba de banco:
 *   byte 0    estado del vehiculo (EcuState), uint8
 *   byte 1    contador de heartbeat, uint8, envuelve en 255
 *
 * Pendiente para el auto definitivo: sumar las flags de presencia de
 * los nodos en los bytes libres.
 */
constexpr uint32_t CAN_ID_ECU_STATE = 0x100;

/**
 * @brief APPS, pedal acelerador. 100 Hz.
 *
 * El acelerador y el freno son dos placas distintas, asi que cada una
 * necesita su propio identificador: dos nodos no pueden compartir una
 * trama porque el bus no tiene forma de saber cual de los dos la emitio.
 *
 * Pendiente: las dos señales del APPS van por separado, porque el
 * chequeo de implausibilidad las necesita crudas. Falta definir escala
 * y endianness contra el hardware real.
 */
constexpr uint32_t CAN_ID_APPS = 0x180;

/**
 * @brief BSE, sensor de presion de freno. 100 Hz.
 *
 * Pendiente: definir escala y rango contra el sensor que se elija.
 */
constexpr uint32_t CAN_ID_BSE = 0x181;

/**
 * @brief RPM ruedas delanteras. 50 Hz.
 *
 * Layout propuesto, a validar cuando exista la placa de RPM:
 *   byte 0-1  RPM rueda izquierda, uint16, little-endian, 1 RPM/bit
 *   byte 2-3  RPM rueda derecha,   uint16, little-endian, 1 RPM/bit
 */
constexpr uint32_t CAN_ID_RPM_FRONT = 0x200;

/**
 * @brief RPM ruedas traseras. 50 Hz.
 *
 * Mismo layout que CAN_ID_RPM_FRONT.
 */
constexpr uint32_t CAN_ID_RPM_REAR = 0x201;

/**
 * @brief RPM en el motor / diferencial. 50 Hz.
 *
 * Es un sensor aparte de los cuatro de rueda: mide del lado del motor,
 * antes del diferencial. Comparado contra las RPM de las ruedas es lo
 * que permite detectar patinamiento.
 *
 * Layout propuesto, a validar cuando exista la placa:
 *   byte 0-1  RPM del motor, uint16, little-endian, 1 RPM/bit
 */
constexpr uint32_t CAN_ID_RPM_MOTOR = 0x202;

/**
 * @brief Volante: angulo y botonera. 50 Hz.
 *
 * Pendiente: falta definir que botones entran, incluido el de RTD.
 */
constexpr uint32_t CAN_ID_STEERING = 0x280;

/**
 * @brief Driver / inversor de traccion. Periodo a definir.
 *
 * Pendiente: el fabricante impone su propio protocolo. Este valor es
 * un lugar reservado hasta tener la hoja de datos; es probable que
 * haya que reemplazarlo por varios IDs.
 */
constexpr uint32_t CAN_ID_DRIVER = 0x300;

/**
 * @brief BMS / bateria. 10 Hz.
 *
 * Pendiente: minimo tension de pack, corriente, temperatura maxima de
 * celda y estado del AIR. Depende del BMS que se elija.
 */
constexpr uint32_t CAN_ID_BMS = 0x380;

/**
 * @brief IMU: acelerometro y giroscopo. 50 Hz.
 *
 * Pendiente: tres ejes de cada sensor no entran en 8 bytes con buena
 * resolucion. Definir si se parte en dos tramas o se recorta.
 */
constexpr uint32_t CAN_ID_IMU = 0x400;

/**
 * @brief TPMS: presion y temperatura de neumaticos. 1 Hz.
 *
 * Pendiente: definir si va una trama por rueda o una sola con indice.
 */
constexpr uint32_t CAN_ID_TPMS = 0x500;

/************************************************************
 *      NODO DE BANCO (layout cerrado, prueba actual)
 ************************************************************/
/*
 * El DHT11 no es un sensor del auto, asi que tiene identificador propio
 * en vez de meterse dentro de una trama del vehiculo que significa otra
 * cosa. Cuando entren los sensores reales, este se retira del mapa.
 *
 * Se eligio 0x480 porque queda entre la IMU (0x400) y el TPMS (0x500):
 * prioridad baja, que es lo que corresponde a un dato que no frena el
 * auto.
 *
 * El otro nodo de la prueba, el de velocidad de giro con el modulo
 * LM393, NO esta aca: usa CAN_ID_RPM_FRONT (0x200), porque medir
 * vueltas por minuto contando pulsos es exactamente lo que va a hacer
 * el sensor definitivo del auto.
 */

/**
 * @brief Nodo de temperatura DHT11. 1 Hz.
 *
 * El DHT11 no admite lecturas mas rapidas que una por segundo, asi que
 * la frecuencia la impone el sensor y no nosotros.
 *
 * Layout cerrado:
 *   byte 0-1  temperatura, int16,  little-endian, 0.1 grados C/bit
 *   byte 2-3  humedad,     uint16, little-endian, 0.1 % HR/bit
 *
 * La temperatura va con signo aunque el DHT11 no mida bajo cero: si
 * despues se cambia por un sensor que si lo hace, el layout ya sirve.
 * La escala 0.1 da un decimal, que es mas resolucion de la que el
 * DHT11 entrega, pero deja lugar a un sensor mejor sin tocar el mapa.
 */
constexpr uint32_t CAN_ID_TEST_TEMPERATURE = 0x480;

