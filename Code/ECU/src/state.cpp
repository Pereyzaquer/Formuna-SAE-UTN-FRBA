/************************************************************
 *  Proyecto : ECU
 *  Archivo  : state.cpp
 *  Equipo   : UTN BA Motorsport Formula student team
 *  Fecha    : 14/8/2026
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Maquina de estados del vehiculo. Unico lugar donde se
 *  decide en que estado esta el auto.
 *
 *  Hardware:
 *  --------------------------------------------------------
 *  - MCU: ESP32 S3.
 *  - Sensores:
 *
 *  Notas:
 *  --------------------------------------------------------
 *  Tabla de transiciones:
 *
 *    BOOT       -> CONFIG      chequeos categoria B OK
 *    BOOT       -> FAULT       falla categoria B (driver o BMS)
 *    CONFIG     -> CAR_READY   sensores flagueados configurados
 *    CAR_READY  -> CAR_ON      secuencia RTD completa
 *    CAR_ON     -> CAR_READY   apagado solicitado
 *    cualquiera -> FAULT       falla CRITICAL
 *    FAULT      -> (sin salida por software)
 *
 *  Categoria A (informativo): si el nodo no responde se marca la
 *  flag, se saltea su configuracion, se registra WARNING y se
 *  sigue. Categoria B (seguridad): si no responde o manda valores
 *  implausibles se va a FAULT y se corta el torque.
 *
 ************************************************************/

/************************************************************
 *                     INCLUDES
 ************************************************************/
#include "../include/ECU.h"
#include "../include/rules.h"

/************************************************************
 *               CONSTANTES DEL SISTEMA
 ************************************************************/
static const uint32_t STATE_PERIOD_MS = 10; /**< Periodo de la task. */

/* TODO: falta el dato. Es el tiempo que BOOT espera a que todos los nodos
   se presenten en el bus. Depende de cuanto tarda en arrancar el nodo mas
   lento (probablemente el BMS): medirlo con el auto armado. */
static const uint32_t BOOT_PRESENCE_TIMEOUT_MS = 2000;

/* TODO: falta la tabla de codigos de falla del equipo. Estos son
   provisorios y tienen que definirse junto con el layout de 0x080. */
static const uint16_t FAULT_CODE_CATEGORY_B = 0x0001;
static const uint16_t FAULT_CODE_NODE_A_LOST = 0x0002;

/************************************************************
 *                VARIABLES GLOBALES
 ************************************************************/
/* volatile: la escribe esta task y la leen taskCan y taskOta. */
static volatile EcuState currentState = EcuState::BOOT;

static bool rtdRequested      = false;
static bool shutdownRequested = false;

/************************************************************
 *             PROTOTIPOS DE FUNCIONES LOCALES
 ************************************************************/
static bool categoryBChecksOk(void);
static bool categoryBFailed(void);
static bool sensorsConfigured(void);
static bool rtdSequenceComplete(void);
static bool shutdownRequestReceived(void);
static void enterState(EcuState next);
static void drainEvents(void);
static void warnMissingCategoryA(void);

/**
 * @brief Devuelve el estado actual del vehiculo.
 *
 * @return EcuState  Estado vigente.
 */
EcuState stateGet(void) {
  return currentState;
}

/**
 * @brief Devuelve el nombre imprimible de un estado.
 *
 * @param[in]  state  Estado a nombrar.
 *
 * @return const char*  Nombre en mayusculas, sin espacios.
 */
const char *stateName(EcuState state) {
  switch (state) {
    case EcuState::FAULT:     return "FAULT";
    case EcuState::BOOT:      return "BOOT";
    case EcuState::CONFIG:    return "CONFIG";
    case EcuState::CAR_READY: return "CAR_READY";
    case EcuState::CAR_ON:    return "CAR_ON";
  }
  return "?";
}

/**
 * @brief Task de la maquina de estados del vehiculo.
 *
 * @param[in]  arg  No se usa. Requerido por la firma de FreeRTOS.
 *
 * @return void
 */
void taskState(void *arg) {
  (void)arg;

  uint32_t bootStart = millis();

  for (;;) {
    drainEvents();

    /* Se evalua antes del switch para que una falla CRITICAL saque al auto
       de cualquier estado sin tener que repetir el chequeo en cada rama. */
    if (faultIsLatched() && currentState != EcuState::FAULT) {
      enterState(EcuState::FAULT);
    }

    switch (currentState) {
      case EcuState::BOOT:
        if (categoryBFailed()) {
          faultReport(FaultLevel::CRITICAL, FAULT_CODE_CATEGORY_B);
          enterState(EcuState::FAULT);
        } else if (millis() - bootStart >= BOOT_PRESENCE_TIMEOUT_MS) {
          /* Se espera el timeout completo antes de decidir: los nodos no
             arrancan todos al mismo tiempo. */
          if (categoryBChecksOk()) {
            warnMissingCategoryA();
            enterState(EcuState::CONFIG);
          } else {
            faultReport(FaultLevel::CRITICAL, FAULT_CODE_CATEGORY_B);
            enterState(EcuState::FAULT);
          }
        }
        break;

      case EcuState::CONFIG:
        if (sensorsConfigured()) {
          enterState(EcuState::CAR_READY);
        }
        break;

      case EcuState::CAR_READY:
        if (rtdSequenceComplete()) {
          rtdRequested = false;
          enterState(EcuState::CAR_ON);
        }
        break;

      case EcuState::CAR_ON:
        if (shutdownRequestReceived()) {
          shutdownRequested = false;
          enterState(EcuState::CAR_READY);
        }
        break;

      case EcuState::FAULT:
        /* Sin salida por software: solo se sale con un reset del micro. */
        break;
    }

    indicatorsUpdate(currentState);
    vTaskDelay(pdMS_TO_TICKS(STATE_PERIOD_MS));
  }
}

/************************************************************
 *                  FUNCIONES LOCALES
 ************************************************************/

/**
 * @brief Aplica el cambio de estado y las acciones de entrada asociadas.
 *
 * Centralizar las acciones de entrada evita que se olvide alguna cuando
 * se agregue una transicion nueva.
 *
 * @param[in]  next  Estado al que se entra.
 *
 * @return void
 */
static void enterState(EcuState next) {
  /* Se imprime antes de cambiar nada: si alguna accion de entrada cuelga
     o resetea, en el serial queda igual el rastro de a donde iba. */
  Serial.printf("[%lu ms] %s -> %s\n", millis(), stateName(currentState),
                stateName(next));

  currentState = next;

  /* El OTA vive mientras el torque este inhibido y se corta apenas se
     habilita: con el auto capaz de moverse no queremos una radio prendida
     ni la posibilidad de reflashear en marcha. En todos los demas estados
     queda disponible a proposito, para poder actualizar la ECU ya montada
     en el auto sin tener que desarmar medio monocasco para sacarla.
     En FAULT tambien queda viva, que es cuando mas falta hace entrar a
     ver que paso. */
  OtaCmd cmd = (next == EcuState::CAR_ON) ? OtaCmd::DISABLE : OtaCmd::ENABLE;
  xQueueSend(qOta, &cmd, 0);

  switch (next) {
    case EcuState::CONFIG:
      sensorsConfigure();
      break;

    case EcuState::CAR_ON:
      /* TODO: habilitar torque en el inversor una vez definido su protocolo. */
      break;

    case EcuState::FAULT:
      /* TODO: cortar torque en el inversor y dejar los nodos en reposo una
         vez definido el protocolo del driver. */
      break;

    default:
      break;
  }
}

/**
 * @brief Consume los eventos que llegan de las otras tasks.
 *
 * @return void
 */
static void drainEvents(void) {
  EcuEvent ev;
  while (xQueueReceive(qEvents, &ev, 0) == pdTRUE) {
    switch (ev.type) {
      case EcuEventType::RTD_REQUEST:      rtdRequested      = true; break;
      case EcuEventType::SHUTDOWN_REQUEST: shutdownRequested = true; break;
    }
  }
}

/**
 * @brief Verifica que los nodos de categoria B esten presentes y sanos.
 *
 * @return bool  true si el auto puede pasar de BOOT a CONFIG.
 */
static bool categoryBChecksOk(void) {
  /* TODO: chequear de verdad flags.driver, flags.bms, flags.apps y flags.bse
     y ademas que sus valores sean plausibles. Devuelve true fijo para poder
     probar la maquina de estados en banco sin el auto armado. */
  return true;
}

/**
 * @brief Verifica si algun nodo de categoria B fallo durante el arranque.
 *
 * @return bool  true si hay que ir directo a FAULT.
 */
static bool categoryBFailed(void) {
  /* TODO: detectar falla explicita del driver o del BMS (no ausencia, que
     la cubre categoryBChecksOk). Devuelve false a proposito: un stub en
     true latchearia FAULT apenas arranca y dejaria el auto muerto. */
  return false;
}

/**
 * @brief Verifica que los nodos flagueados terminaron de configurarse.
 *
 * @return bool  true si el auto puede pasar de CONFIG a CAR_READY.
 */
static bool sensorsConfigured(void) {
  /* TODO: esperar el acuse de cada nodo configurado en sensorsConfigure().
     Hoy no hay mensaje de configuracion, asi que se pasa de largo. */
  return true;
}

/**
 * @brief Verifica que la secuencia de Ready To Drive este completa.
 *
 * @return bool  true si el auto puede pasar de CAR_READY a CAR_ON.
 */
static bool rtdSequenceComplete(void) {
  /* TODO: el reglamento exige freno accionado + boton de RTD con el tractive
     system activo, y el sonido de RTD antes de habilitar torque. Falta
     validar el articulo y sumar el freno (rules.h::RULE_BSE_ACTUATED_BAR)
     y el estado del tractive system. Por ahora alcanza con el boton. */
  return rtdRequested;
}

/**
 * @brief Verifica si se pidio apagar el torque.
 *
 * @return bool  true si el auto debe volver de CAR_ON a CAR_READY.
 */
static bool shutdownRequestReceived(void) {
  /* TODO: definir que ademas del boton cuenta como pedido de apagado. */
  return shutdownRequested;
}

/**
 * @brief Registra un WARNING por cada nodo de categoria A ausente.
 *
 * Un nodo informativo caido no impide correr: solo queda asentado para
 * que se vea en telemetria y en el pit.
 *
 * @return void
 */
static void warnMissingCategoryA(void) {
  SensorFlags f = sensorsGetFlags();

  if (!f.rpmFront || !f.rpmRear || !f.steering || !f.imu || !f.tpms) {
    /* TODO: usar un codigo distinto por nodo cuando exista la tabla de
       codigos de falla, para poder identificar cual falto. */
    faultReport(FaultLevel::WARNING, FAULT_CODE_NODE_A_LOST);
  }
}
