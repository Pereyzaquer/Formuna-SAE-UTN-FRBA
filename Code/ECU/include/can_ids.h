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
 *  Los IDs mas bajos tienen mayor prioridad de arbitraje, por eso
 *  la emergencia queda en 0x080 y la telemetria lenta arriba.
 *  El layout de bytes de cada trama todavia no esta cerrado: cada
 *  constante lleva su TODO con lo que falta definir.
 *
 ************************************************************/
#pragma once

#include <stdint.h>

/**
 * @brief Falla / emergencia. Asincrono.
 *
 * ID mas bajo del mapa para que gane el arbitraje contra todo lo demas.
 *
 * TODO: definir layout. Minimo hace falta nodo origen, codigo de falla y
 *       nivel (FaultLevel). Acordar la tabla de codigos con todo el equipo.
 */
constexpr uint32_t CAN_ID_FAULT = 0x080;

/**
 * @brief Estado de la ECU + heartbeat. 100 Hz.
 *
 * TODO: definir layout. Minimo estado (EcuState), contador de heartbeat y
 *       resumen de flags de sensores.
 */
constexpr uint32_t CAN_ID_ECU_STATE = 0x100;

/**
 * @brief APPS + BSE. 100 Hz.
 *
 * TODO: definir layout. Van las dos señales de APPS por separado (el
 *       chequeo de implausibilidad las necesita crudas) mas la presion de
 *       freno. Definir escala y endianness.
 */
constexpr uint32_t CAN_ID_APPS_BSE = 0x180;

/**
 * @brief RPM ruedas delanteras. 50 Hz.
 *
 * TODO: definir layout. Dos ruedas por trama, unidad y escala a confirmar
 *       con la placa de RPM.
 */
constexpr uint32_t CAN_ID_RPM_FRONT = 0x200;

/**
 * @brief RPM ruedas traseras. 50 Hz.
 *
 * TODO: definir layout. Mismo formato que CAN_ID_RPM_FRONT.
 */
constexpr uint32_t CAN_ID_RPM_REAR = 0x201;

/**
 * @brief Volante: angulo y botonera. 50 Hz.
 *
 * TODO: definir layout. Falta saber que botones entran, incluido el de RTD.
 */
constexpr uint32_t CAN_ID_STEERING = 0x280;

/**
 * @brief Driver / inversor de traccion. Periodo TBD.
 *
 * TODO: el fabricante del inversor impone su propio protocolo (IDs, layout
 *       y periodo). Este valor es un placeholder hasta tener la hoja de
 *       datos; muy probablemente haya que reemplazarlo por varios IDs.
 */
constexpr uint32_t CAN_ID_DRIVER = 0x300;

/**
 * @brief BMS / bateria. 10 Hz.
 *
 * TODO: definir layout. Minimo tension de pack, corriente, temperatura
 *       maxima de celda y estado del AIR. Depende del BMS elegido.
 */
constexpr uint32_t CAN_ID_BMS = 0x380;

/**
 * @brief IMU: acelerometro y giroscopo. 50 Hz.
 *
 * TODO: definir layout. Tres ejes de cada uno no entran en 8 bytes con
 *       buena resolucion: definir si se parte en dos tramas o se recorta.
 */
constexpr uint32_t CAN_ID_IMU = 0x400;

/**
 * @brief TPMS: presion y temperatura de neumaticos. 1 Hz.
 *
 * TODO: definir layout. Definir si va una trama por rueda o una sola con
 *       las cuatro y un indice.
 */
constexpr uint32_t CAN_ID_TPMS = 0x500;
