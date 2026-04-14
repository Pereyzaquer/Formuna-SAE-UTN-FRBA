/************************************************************
 *  Proyecto : RPM_Sensor
 *  Archivo  : funtions.cpp
 *  Equipo   : UTN BA Motorsport Formula student team 
 *  Fecha    : 8/1/2026
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Funciones de adquisicion de datos, configuracion de
 *  sensores, etc.
 *
 *  Hardware:
 *  --------------------------------------------------------
 *  - MCU: ESP32 mini.
 *  - Sensores: 
 *
 *  Notas:
 *  --------------------------------------------------------
 *
 ************************************************************/

/************************************************************
 *                     INCLUDES
 ************************************************************/
#include "../include/RPM.h"

/************************************************************
 *                VARIABLES GLOBALES EXTERNAS
 ************************************************************/

volatile unsigned long last_pulse_time; // guarda el tiempo donde ocurrio el ultimo pulso
volatile unsigned long pulse_interval;  // Difercia de tiempo entre dos detecciones

//FUNCIONES QUE DEJE DEL PROGRAMA DE PRUEBA PARA PROBAR LOS ESTADOS CON EL LED RGB QUE TIENE LA PLACA (LA ESP32 S3, NO LA ESP8266)

/**
 * @brief Inicializa el led RGB de la placa.
 *
 * @param[in]  void
 *
 * @return void           
 */
void rgb_init() {
  
}

/*
 *  Funciona para el conteo de pulsos del sensor
 *  Calcula el intervalo de tiempo entre flancos detectado

    IRAM_ATTR: funcion se ejecute desde la memoria RAM interna
    para no perder pulsos a altas rev.

*/

void IRAM_ATTR handle_rpm(void)
{
    unsigned long now = micros();
    unsigned long delta = now - last_pulse_time;

    // buscamos evitar rebotes/ruido  (3000us = aprox 20000 RPM max)

    if(delta > 3000)
    {
        pulse_interval = delta;
        last_pulse_time = now;
    }
}


/*
    Inicializacion del sensor:

    Habilita la resistencia pull-up (estado natural HIGH) interna y vincula el flanco.

    Para la configuracion de la interrupcion
    - digitalPinToInterrupt: convierte el numero de pin al canal de interrupcion
    Configuracion del flanco:
    - RISING: voltaje pasa de LOW a HIGH
    - FALLING: pasa de HIGH a LOW

    Para el sensor conviene FALLING, interrupcion se dispara cuando la señal cae a 0V
*/

void initSensor(void)
{
    pinMode(INT_PIN, INPUT_PULLUP);

    attachInterrupt(digitalPinToInterrupt(INT_PIN), handle_rpm, FALLING);
}

/*
*   Calcula la velocidad de rotacion actual en RPM
*   Convierte intervalo de tiempo entre pulsos (microseg) a rev por minuto.
*   
*   return: valor de RPM calculado
*/

float getRPM(void)
{
    // aplicamos timeout de 1 seg
    if((micros() - last_pulse_time) > 1000000 )
    {
        return 0.0;
    }else if(pulse_interval == 0)
    {
        return 0.0;
    }else{
        return 60000000.0 / pulse_interval;
    }

}

