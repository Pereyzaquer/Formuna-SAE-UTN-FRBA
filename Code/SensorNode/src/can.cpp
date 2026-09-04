/************************************************************
 *  Proyecto : SensorNode
 *  Archivo  : can.cpp
 *  Equipo   : UTN BA Motorsport Formula student team
 *  Fecha    : 2/9/2026
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Bus CAN del nodo sobre el periferico TWAI del ESP32.
 *  Solo transmite: este nodo no escucha a nadie.
 *
 *  Hardware:
 *  --------------------------------------------------------
 *  - MCU: ESP32 C3.
 *  - Sensores: ninguno propio.
 *
 *  Notas:
 *  --------------------------------------------------------
 *  Bus a 500 kbps, la misma velocidad que la ECU. Si un nodo
 *  usara otra, no solo no se entenderia: ensuciaria el bus para
 *  todos los demas.
 *
 ************************************************************/

/************************************************************
 *                     INCLUDES
 ************************************************************/
#include "../include/SensorNode.h"

/**
 * @brief Inicializa el periferico TWAI a 500 kbps en modo normal.
 *
 * El filtro acepta todo aunque este nodo no procese nada de lo que
 * recibe. Dejarlo asi permite conectarle un analizador y ver el bus sin
 * recompilar, y no cuesta nada.
 *
 * @return bool  true si el driver quedo instalado y arrancado.
 */
bool canInitialize(void) {
  twai_general_config_t generalConfiguration = TWAI_GENERAL_CONFIG_DEFAULT(
      (gpio_num_t)CAN_TRANSMIT_PIN, (gpio_num_t)CAN_RECEIVE_PIN,
      TWAI_MODE_NORMAL);
  twai_timing_config_t timingConfiguration = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t filterConfiguration = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&generalConfiguration, &timingConfiguration,
                          &filterConfiguration) != ESP_OK) {
    return false;
  }

  return (twai_start() == ESP_OK);
}

/**
 * @brief Transmite una trama estandar por el bus.
 *
 * @param[in]  identifier  Identificador CAN de 11 bits.
 * @param[in]  data        Puntero a los bytes a enviar.
 * @param[in]  length      Cantidad de bytes, de 0 a 8.
 *
 * @return bool  true si la trama entro en la cola de transmision.
 */
bool canSendFrame(uint32_t identifier, const uint8_t *data, uint8_t length) {
  if (length > 8) {
    return false;
  }

  twai_message_t frame = {};
  frame.identifier       = identifier;
  frame.data_length_code = length;
  frame.extd             = 0; /* Todo el mapa usa identificadores de 11 bits. */

  for (uint8_t index = 0; index < length; index++) {
    frame.data[index] = data[index];
  }

  /* Sin espera: si la cola esta llena, perder una medicion es mejor que
     frenar el bucle del nodo y atrasar todas las que vienen. */
  return (twai_transmit(&frame, 0) == ESP_OK);
}

/**
 * @brief Guarda un entero de 16 bits en dos bytes, little-endian.
 *
 * @param[out]  data   Puntero a los dos bytes de destino.
 * @param[in]   value  Valor a guardar.
 *
 * @return void
 */
void writeInteger16LittleEndian(uint8_t *data, int16_t value) {
  data[0] = static_cast<uint8_t>(value & 0xFF);
  data[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}
