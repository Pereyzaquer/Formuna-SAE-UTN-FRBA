/************************************************************
 *  Proyecto : SensorNode
 *  Archivo  : can_mcp2515.cpp
 *  Equipo   : UTN BA Motorsport Formula student team
 *  Fecha    : 10/9/2026
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Bus CAN del nodo con un modulo MCP2515 externo por SPI.
 *  Solo transmite: este nodo no escucha a nadie.
 *
 *  Hardware:
 *  --------------------------------------------------------
 *  - MCU: Arduino Nano, ESP32-C3 o NodeMCU, segun el entorno.
 *  - Modulo HW-184: chip MCP2515 (controlador CAN) mas
 *    TJA1050 (transceiver), con cristal propio. Es de 5 V.
 *      SCK, SI, SO -> pines SPI fijos de cada placa
 *      CS          -> CAN_SPI_CHIP_SELECT_PIN
 *      INT         -> sin conectar, este nodo no recibe
 *
 *  En el Nano va directo. En las placas de 3.3 V hay que bajar
 *  de nivel la linea SO, que vuelve con 5 V: ver SensorNode.h y
 *  ARQUITECTURA.md para cada caso.
 *
 *  Notas:
 *  --------------------------------------------------------
 *  El MCP2515 lleva su propio cristal y el codigo tiene que
 *  saber de cuantos MHz es, porque de eso depende la velocidad
 *  del bus. Si la constante no coincide con el cristal, el bus
 *  queda a otra velocidad y nadie se entiende. El valor esta
 *  grabado en la lata metalica del modulo.
 *
 *  Bus a 500 kbps, la misma velocidad que la ECU.
 *
 *  Se compila en todos los entornos de la prueba de banco. El
 *  otro backend, can_twai.cpp, es para el C3 con transceiver.
 *
 ************************************************************/

/************************************************************
 *                     INCLUDES
 ************************************************************/
#include "../include/SensorNode.h"
#include <SPI.h>
#include <mcp2515.h>

/************************************************************
 *               CONSTANTES DEL SISTEMA
 ************************************************************/

/**
 * Cristal del modulo MCP2515.
 *
 * Los cuatro HW-184 del banco tienen cristal de 8.000 MHz (esta
 * grabado en la lata metalica). Si se cambia un modulo, revisarlo: con
 * un cristal de 16 MHz el bus queda a otra velocidad y nadie se
 * entiende, sin ningun mensaje de error que lo diga.
 */
static const CAN_CLOCK MCP2515_CRYSTAL = MCP_8MHZ;

/************************************************************
 *                VARIABLES GLOBALES
 ************************************************************/
/* El SPI de cada placa tiene pines fijos, asi que el controlador puede
   construirse como global sin configurar nada antes. La velocidad del
   SPI la fija SensorNode.h por placa: baja donde hay conversor de nivel
   o divisor en el medio, porque deforman los flancos. */
static MCP2515 controller(CAN_SPI_CHIP_SELECT_PIN, CAN_SPI_CLOCK_HZ);

/**
 * @brief Deja el controlador CAN andando a 500 kbps.
 *
 * @return bool  true si quedo listo para transmitir.
 */
bool canInitialize(void) {
  if (controller.reset() != MCP2515::ERROR_OK) {
    return false;
  }
  if (controller.setBitrate(CAN_500KBPS, MCP2515_CRYSTAL) != MCP2515::ERROR_OK) {
    return false;
  }
  return (controller.setNormalMode() == MCP2515::ERROR_OK);
}

/**
 * @brief Transmite una trama estandar por el bus.
 *
 * El MCP2515 tiene tres buffers de salida. Si estan los tres ocupados
 * la trama se descarta: perder una medicion es mejor que frenar el
 * bucle del nodo y atrasar todas las que vienen.
 *
 * @param[in]  identifier  Identificador CAN de 11 bits.
 * @param[in]  data        Puntero a los bytes a enviar.
 * @param[in]  length      Cantidad de bytes, de 0 a 8.
 *
 * @return bool  true si el controlador acepto la trama.
 */
bool canSendFrame(uint32_t identifier, const uint8_t *data, uint8_t length) {
  if (length > 8) {
    return false;
  }

  struct can_frame message;
  message.can_id  = identifier; /* Sin flags: identificador estandar. */
  message.can_dlc = length;

  for (uint8_t index = 0; index < length; index++) {
    message.data[index] = data[index];
  }

  return (controller.sendMessage(&message) == MCP2515::ERROR_OK);
}
