/************************************************************
 *  Proyecto : ECU
 *  Archivo  : can_mcp2515.cpp
 *  Equipo   : UTN BA Motorsport Formula student team
 *  Fecha    : 10/9/2026
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Capa de hardware del bus CAN usando un modulo MCP2515
 *  externo, conectado por SPI. Es el backend de la prueba de
 *  banco, porque es el hardware que hay.
 *
 *  Hardware:
 *  --------------------------------------------------------
 *  - MCU: ESP32 S3.
 *  - Modulo HW-184: chip MCP2515 (controlador CAN) mas
 *    TJA1050 (transceiver), con cristal propio.
 *  - Conversor de nivel de 4 canales entre los dos: el modulo
 *    es de 5 V y el ESP32 de 3.3 V.
 *
 *  Notas:
 *  --------------------------------------------------------
 *  El MCP2515 lleva su propio cristal y el codigo tiene que
 *  saber de cuantos MHz es, porque de eso depende la velocidad
 *  del bus. Si la constante no coincide con el cristal, el bus
 *  queda a otra velocidad y nadie se entiende. El valor esta
 *  grabado en la lata metalica del modulo.
 *
 *  El SPI va a 1 MHz y no mas rapido, porque pasa por un
 *  conversor de nivel a MOSFET que no aguanta mucho mas.
 *
 *  Se compila solo en el entorno "mcp2515" de platformio.ini.
 *  El otro backend es can_twai.cpp.
 *
 ************************************************************/

/************************************************************
 *                     INCLUDES
 ************************************************************/
#include "../include/ECU.h"
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

/**
 * Velocidad del SPI hacia el modulo.
 *
 * Bajo a proposito: las cuatro lineas pasan por un conversor de nivel a
 * MOSFET, que a velocidades altas deforma los flancos y corrompe los
 * bytes. A 1 MHz sobra para lo que se mueve por este bus.
 */
static const uint32_t SPI_CLOCK_HZ = 1000000;

/************************************************************
 *                VARIABLES GLOBALES
 ************************************************************/
/* El bus SPI y el controlador se crean en canHardwareInitialize() y no
   como globales comunes, porque hay que fijar los pines del SPI antes de
   que el controlador lo use, y una global se construye antes de que
   corra cualquier codigo nuestro. */
static SPIClass *spiBus = nullptr;
static MCP2515  *controller = nullptr;

static bool wasBusOff = false;

/**
 * @brief Deja el controlador CAN andando a 500 kbps.
 *
 * @return bool  true si quedo listo para transmitir y recibir.
 */
bool canHardwareInitialize(void) {
  spiBus = new SPIClass(FSPI);
  spiBus->begin(CAN_SPI_CLOCK_PIN, CAN_SPI_MASTER_IN_PIN,
                CAN_SPI_MASTER_OUT_PIN, CAN_SPI_CHIP_SELECT_PIN);

  controller = new MCP2515(CAN_SPI_CHIP_SELECT_PIN, SPI_CLOCK_HZ, spiBus);

  if (controller->reset() != MCP2515::ERROR_OK) {
    return false;
  }
  if (controller->setBitrate(CAN_500KBPS, MCP2515_CRYSTAL) != MCP2515::ERROR_OK) {
    return false;
  }
  return (controller->setNormalMode() == MCP2515::ERROR_OK);
}

/**
 * @brief Entrega una trama al controlador para que la transmita.
 *
 * El MCP2515 tiene tres buffers de salida. Si estan los tres ocupados,
 * la trama se descarta en vez de esperar: preferimos perderla antes que
 * frenar a la task que la mando.
 *
 * @param[in]  frame  Trama a transmitir.
 *
 * @return bool  true si el controlador la acepto.
 */
bool canHardwareSend(const CanFrame *frame) {
  struct can_frame message;
  message.can_id  = frame->identifier;
  message.can_dlc = frame->length;

  for (uint8_t index = 0; index < frame->length; index++) {
    message.data[index] = frame->data[index];
  }

  return (controller->sendMessage(&message) == MCP2515::ERROR_OK);
}

/**
 * @brief Espera una trama entrante, como maximo el tiempo indicado.
 *
 * El MCP2515 no avisa por si solo, asi que se le pregunta cada
 * milisegundo hasta que aparezca algo o se acabe el tiempo.
 *
 * @param[out]  frame                Trama recibida.
 * @param[in]   timeoutMilliseconds  Cuanto esperar si no hay nada.
 *
 * @return bool  true si llego una trama.
 */
bool canHardwareReceive(CanFrame *frame, uint32_t timeoutMilliseconds) {
  struct can_frame message;
  uint32_t startMilliseconds = millis();

  for (;;) {
    if (controller->readMessage(&message) == MCP2515::ERROR_OK) {
      /* La libreria mete flags en los bits altos del identificador para
         marcar tramas extendidas o remotas. Solo interesan los 11 bits
         del identificador estandar. */
      frame->identifier = message.can_id & CAN_SFF_MASK;
      frame->length     = message.can_dlc;

      for (uint8_t index = 0; index < message.can_dlc && index < 8; index++) {
        frame->data[index] = message.data[index];
      }
      return true;
    }

    if (millis() - startMilliseconds >= timeoutMilliseconds) {
      return false;
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

/**
 * @brief Revisa si el controlador se cayo del bus y lo reengancha.
 *
 * A diferencia del TWAI, el MCP2515 se recupera solo del estado "bus
 * off": vuelve al bus cuando lo ve tranquilo un rato. Aca solo hace
 * falta detectar la caida para poder contarla, y se cuenta una vez por
 * caida y no una vez por llamada.
 *
 * @return bool  true si en esta llamada se detecto una caida nueva.
 */
bool canHardwareRecoverIfNeeded(void) {
  bool isBusOff = (controller->getErrorFlags() & MCP2515::EFLG_TXBO) != 0;

  bool newFall = isBusOff && !wasBusOff;
  wasBusOff = isBusOff;

  return newFall;
}
