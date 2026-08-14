/************************************************************
 *  Proyecto : ECU
 *  Archivo  : rules.h
 *  Equipo   : UTN BA Motorsport Formula student team
 *  Fecha    : 14/8/2026
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Umbrales exigidos por el reglamento de Formula Student.
 *  Fuente unica de verdad: estos valores no se repiten en
 *  ningun otro archivo.
 *
 *  Notas:
 *  --------------------------------------------------------
 *  ATENCION: la numeracion de reglas cambia entre ediciones del
 *  rulebook. Antes de cada competencia hay que revisar uno por uno
 *  estos valores contra el reglamento de la edicion vigente y
 *  actualizar la referencia que figura en cada comentario.
 *
 ************************************************************/
#pragma once

#include <stdint.h>

/**
 * @brief Desviacion maxima admitida entre las dos señales del APPS.
 *
 * Regla de implausibilidad del acelerador (familia T.4 / EV.4 segun
 * edicion): si las dos señales difieren mas de este porcentaje del
 * recorrido de pedal y la condicion se sostiene mas de
 * RULE_APPS_IMPLAUSIBILITY_TIME_MS, hay que cortar el torque.
 *
 * TODO: validar el 10% y el articulo exacto contra el rulebook vigente.
 */
constexpr float RULE_APPS_IMPLAUSIBILITY_PCT = 10.0f;

/**
 * @brief Tiempo que debe sostenerse la implausibilidad antes de cortar.
 *
 * Misma regla que RULE_APPS_IMPLAUSIBILITY_PCT. Por debajo de este
 * tiempo la desviacion se ignora (evita cortes por ruido).
 *
 * TODO: validar los 100 ms contra el rulebook vigente.
 */
constexpr uint32_t RULE_APPS_IMPLAUSIBILITY_TIME_MS = 100;

/**
 * @brief Recorrido de pedal a partir del cual, con freno accionado, se corta.
 *
 * Regla de plausibilidad APPS / freno (familia EV.5 segun edicion):
 * acelerador por encima de este umbral con el freno accionado obliga a
 * cortar el torque.
 *
 * TODO: el reglamento fija este umbral (historicamente 25% del recorrido).
 *       Confirmar valor y articulo antes de usarlo en pista.
 */
constexpr float RULE_APPS_BRAKE_CUT_PCT = 25.0f;

/**
 * @brief Recorrido de pedal por debajo del cual se rearma el torque.
 *
 * Continuacion de la regla anterior: una vez cortado por plausibilidad
 * APPS/freno, el torque solo vuelve cuando el acelerador regresa a
 * ralenti, aunque el piloto ya haya soltado el freno.
 *
 * TODO: el reglamento habla de "menos del 5% del recorrido". Confirmar
 *       valor y articulo contra el rulebook vigente.
 */
constexpr float RULE_APPS_REARM_PCT = 5.0f;

/**
 * @brief Presion de freno a partir de la cual se considera freno accionado.
 *
 * Umbral de deteccion propio, no impuesto por reglamento, pero necesario
 * para evaluar RULE_APPS_BRAKE_CUT_PCT.
 *
 * TODO: falta el dato. Depende del sensor de presion elegido y de la
 *       presion de linea medida en el auto. Medir en banco antes de fijarlo.
 */
constexpr float RULE_BSE_ACTUATED_BAR = 0.0f;
