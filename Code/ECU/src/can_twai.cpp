/************************************************************
 *  Proyecto : ECU
 *  Archivo  : can_twai.cpp
 *  Equipo   : UTN BA Motorsport Formula student team
 *  Fecha    : 10/9/2026
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Capa de hardware del bus CAN usando el controlador interno
 *  del ESP32, que Espressif llama TWAI. Es el backend
 *  definitivo para el auto.
 *
 *  Hardware:
 *  --------------------------------------------------------
 *  - MCU: ESP32 S3.
 *  - Transceiver de 3.3 V, tipo SN65HVD230, en los pines
 *    CAN_TRANSMIT_PIN y CAN_RECEIVE_PIN.
 *
 *  Notas:
 *  --------------------------------------------------------
 *  TWAI es CAN 2.0 con otro nombre: Espressif no puede usar la
 *  marca. El periferico esta adentro del chip y habla en
 *  niveles logicos de 3.3 V por dos pines; el transceiver los
 *  convierte al bus diferencial.
 *
 *  Se compila solo en el entorno "twai" de platformio.ini. El
 *  otro backend es can_mcp2515.cpp.
 *
 ************************************************************/

/************************************************************
 *                     INCLUDES
 ************************************************************/
#include "../include/ECU.h"
#include <driver/twai.h>

/**
 * @brief Deja el controlador CAN andando a 500 kbps.
 *
 * Se acepta todo el trafico del bus porque la ECU necesita ver a todos
 * los nodos para saber cuales estan presentes, incluso los que todavia
 * no sabe interpretar.
 *
 * @return bool  true si quedo listo para transmitir y recibir.
 */
bool canHardwareInitialize(void) {
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
 * @brief Entrega una trama al controlador para que la transmita.
 *
 * Sin espera: si la cola de transmision esta llena preferimos perder la
 * trama antes que bloquear a la task que la mando.
 *
 * @param[in]  frame  Trama a transmitir.
 *
 * @return bool  true si el controlador la acepto.
 */
bool canHardwareSend(const CanFrame *frame) {
  twai_message_t message = {};
  message.identifier       = frame->identifier;
  message.data_length_code = frame->length;
  message.extd             = 0; /* Todo el mapa usa identificadores de 11 bits. */

  for (uint8_t index = 0; index < frame->length; index++) {
    message.data[index] = frame->data[index];
  }

  return (twai_transmit(&message, 0) == ESP_OK);
}

/**
 * @brief Espera una trama entrante, como maximo el tiempo indicado.
 *
 * @param[out]  frame                Trama recibida.
 * @param[in]   timeoutMilliseconds  Cuanto esperar si no hay nada.
 *
 * @return bool  true si llego una trama.
 */
bool canHardwareReceive(CanFrame *frame, uint32_t timeoutMilliseconds) {
  twai_message_t message;

  if (twai_receive(&message, pdMS_TO_TICKS(timeoutMilliseconds)) != ESP_OK) {
    return false;
  }

  frame->identifier = message.identifier;
  frame->length     = message.data_length_code;

  for (uint8_t index = 0; index < message.data_length_code && index < 8;
       index++) {
    frame->data[index] = message.data[index];
  }

  return true;
}

/**
 * @brief Revisa si el controlador se cayo del bus y lo reengancha.
 *
 * El TWAI no se recupera solo del estado "bus off". La recuperacion
 * tiene dos tiempos: primero se pide, y el hardware espera a que el bus
 * este tranquilo. Cuando termina queda detenido, y recien ahi se lo
 * vuelve a arrancar. Por eso el estado STOPPED se trata aca como
 * "termino de recuperarse": canHardwareInitialize() ya lo arranco antes
 * de que existiera la task, asi que STOPPED no puede significar otra
 * cosa.
 *
 * @return bool  true si en esta llamada se detecto una caida.
 */
bool canHardwareRecoverIfNeeded(void) {
  twai_status_info_t status;

  if (twai_get_status_info(&status) != ESP_OK) {
    return false;
  }

  if (status.state == TWAI_STATE_BUS_OFF) {
    twai_initiate_recovery();
    return true;
  }

  if (status.state == TWAI_STATE_STOPPED) {
    twai_start();
  }

  return false;
}
