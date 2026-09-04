/************************************************************
 *  Proyecto : ECU
 *  Archivo  : state.cpp
 *  Equipo   : UTN BA Motorsport Formula student team
 *  Fecha    : 2/9/2026
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Maquina de estados del vehiculo. Unico lugar donde se
 *  decide en que estado esta el auto.
 *
 *  Hardware:
 *  --------------------------------------------------------
 *  - MCU: ESP32 S3.
 *  - Sensores: boton de Ready To Drive en un pin.
 *
 *  Notas:
 *  --------------------------------------------------------
 *  Tabla de transiciones:
 *
 *    BOOT       -> CONFIG      chequeos categoria B en orden
 *    BOOT       -> FAULT       falla categoria B
 *    CONFIG     -> CAR_READY   nodos presentes configurados
 *    CAR_READY  -> CAR_ON      secuencia RTD completa
 *    CAR_ON     -> CAR_READY   apagado pedido, o ejecucion terminada
 *    cualquiera -> FAULT       falla CRITICAL
 *    FAULT      -> sin salida por software
 *
 *  Categoria A, informativo: si el nodo no responde se marca la
 *  flag, se saltea su configuracion, se registra WARNING y se
 *  sigue. Categoria B, seguridad: si no responde o manda valores
 *  implausibles se pasa a FAULT y se corta el torque.
 *
 *  En la prueba de banco no hay ningun nodo de categoria B en el
 *  bus, asi que esos chequeos estan escritos pero dan por buena
 *  la situacion. Queda pendiente activarlos cuando existan el
 *  inversor y el BMS.
 *
 ************************************************************/

/************************************************************
 *                     INCLUDES
 ************************************************************/
#include "../include/ECU.h"

/************************************************************
 *               CONSTANTES DEL SISTEMA
 ************************************************************/
static const uint32_t STATE_PERIOD_MILLISECONDS = 10;

/**
 * Tiempo que BOOT espera a que los nodos se presenten en el bus.
 *
 * Definido en 2 segundos para la prueba: alcanza de sobra para que un
 * ESP32-C3 arranque y mande su primera trama. Pendiente medirlo con el
 * auto armado, porque lo va a fijar el nodo mas lento, probablemente
 * el BMS.
 */
static const uint32_t NODE_DISCOVERY_MILLISECONDS = 2000;

/** Tiempo que el boton tiene que quedar quieto para creerle. */
static const uint32_t BUTTON_DEBOUNCE_MILLISECONDS = 50;

/**
 * Wifi encendido tambien en CAR_ON.
 *
 * En el auto definitivo el wifi tiene que apagarse cuando el torque se
 * habilita. Durante esta prueba los datos salen justamente por wifi
 * mientras se graba, que ocurre en CAR_ON, asi que queda encendido.
 * Poner esto en false restablece el comportamiento del auto.
 */
static const bool WIFI_STAYS_ON_IN_CAR_ON = true;

/*
 * Codigos de falla. Definidos para la prueba; pendiente unificarlos en
 * una tabla del equipo cuando se cierre el layout de la trama 0x080.
 */
static const uint16_t ERROR_CODE_SAFETY_NODE_MISSING = 0x0001;
static const uint16_t ERROR_CODE_INFORMATIVE_NODE_MISSING = 0x0002;

/************************************************************
 *                VARIABLES GLOBALES
 ************************************************************/
/* volatile: la escribe esta task y la leen taskCan y el monitoreo web. */
static volatile EcuState currentState = EcuState::BOOT;

static bool readyToDriveRequested = false;
static bool shutdownRequested     = false;

/************************************************************
 *             PROTOTIPOS DE FUNCIONES LOCALES
 ************************************************************/
static bool safetyNodesAreHealthy(void);
static bool safetyNodeHasFailed(void);
static bool presentNodesAreConfigured(void);
static bool readyToDriveSequenceIsComplete(void);
static bool shutdownWasRequested(void);
static bool readyToDriveButtonIsPressed(void);
static void warnAboutMissingInformativeNodes(void);
static void enterState(EcuState nextState);
static void processPendingEvents(void);

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
const char *stateGetName(EcuState state) {
  switch (state) {
    case EcuState::FAULT:     return "FAULT";
    case EcuState::BOOT:      return "BOOT";
    case EcuState::CONFIG:    return "CONFIG";
    case EcuState::CAR_READY: return "CAR_READY";
    case EcuState::CAR_ON:    return "CAR_ON";
  }
  return "DESCONOCIDO";
}

/**
 * @brief Task de la maquina de estados del vehiculo.
 *
 * @param[in]  argument  No se usa. Lo exige la firma de FreeRTOS.
 *
 * @return void
 */
void taskState(void *argument) {
  (void)argument;

  pinMode(READY_TO_DRIVE_BUTTON_PIN, INPUT_PULLUP);

  uint32_t discoveryStartMilliseconds = millis();

  for (;;) {
    processPendingEvents();

    /* Se evalua antes del switch para que una falla CRITICAL saque al
       auto de cualquier estado sin repetir el chequeo en cada rama. */
    if (errorIsLatched() && currentState != EcuState::FAULT) {
      enterState(EcuState::FAULT);
    }

    switch (currentState) {
      case EcuState::BOOT:
        if (safetyNodeHasFailed()) {
          errorReport(ErrorLevel::CRITICAL, ERROR_CODE_SAFETY_NODE_MISSING);
          enterState(EcuState::FAULT);
        } else if (millis() - discoveryStartMilliseconds >=
                   NODE_DISCOVERY_MILLISECONDS) {
          /* Se espera la ventana completa antes de decidir: los nodos
             no arrancan todos al mismo tiempo. */
          if (safetyNodesAreHealthy()) {
            warnAboutMissingInformativeNodes();
            enterState(EcuState::CONFIG);
          } else {
            errorReport(ErrorLevel::CRITICAL, ERROR_CODE_SAFETY_NODE_MISSING);
            enterState(EcuState::FAULT);
          }
        }
        break;

      case EcuState::CONFIG:
        if (presentNodesAreConfigured()) {
          enterState(EcuState::CAR_READY);
        }
        break;

      case EcuState::CAR_READY:
        if (readyToDriveSequenceIsComplete()) {
          readyToDriveRequested = false;
          enterState(EcuState::CAR_ON);
        }
        break;

      case EcuState::CAR_ON:
        /* La ejecucion termina sola al cumplirse la duracion. Tambien se
           puede cortar antes pidiendo el apagado. */
        if (!loggerIsRecording() || shutdownWasRequested()) {
          shutdownRequested = false;
          enterState(EcuState::CAR_READY);
        }
        break;

      case EcuState::FAULT:
        /* Sin salida por software: solo se sale reiniciando el micro. */
        break;
    }

    indicatorsUpdate(currentState);
    vTaskDelay(pdMS_TO_TICKS(STATE_PERIOD_MILLISECONDS));
  }
}

/************************************************************
 *                  FUNCIONES LOCALES
 ************************************************************/

/**
 * @brief Aplica el cambio de estado y las acciones de entrada.
 *
 * Concentrar las acciones de entrada en un solo lugar evita que se
 * olvide alguna cuando se agregue una transicion nueva.
 *
 * @param[in]  nextState  Estado al que se entra.
 *
 * @return void
 */
static void enterState(EcuState nextState) {
  /* Se imprime antes de cambiar nada: si una accion de entrada cuelga o
     reinicia, en el puerto serie queda igual el rastro de a donde iba. */
  Serial.printf("[%lu ms] %s -> %s\n", millis(), stateGetName(currentState),
                stateGetName(nextState));

  currentState = nextState;

  WifiCommand wifiCommand =
      (nextState == EcuState::CAR_ON && !WIFI_STAYS_ON_IN_CAR_ON)
          ? WifiCommand::DISABLE
          : WifiCommand::ENABLE;
  xQueueSend(queueWifi, &wifiCommand, 0);

  switch (nextState) {
    case EcuState::CAR_ON:
      /* Entrar en CAR_ON es lo que dispara la ejecucion de la prueba.
         Pendiente para el auto: aca tambien va la habilitacion de
         torque en el inversor. */
      loggerStart();
      break;

    case EcuState::CAR_READY:
      loggerStop();
      break;

    case EcuState::FAULT:
      loggerStop();
      /* Pendiente para el auto: cortar el torque en el inversor y dejar
         los nodos en reposo, una vez definido su protocolo. */
      break;

    default:
      break;
  }
}

/**
 * @brief Consume los eventos que mandan las otras tasks.
 *
 * @return void
 */
static void processPendingEvents(void) {
  EcuEvent event;

  while (xQueueReceive(queueEvents, &event, 0) == pdTRUE) {
    switch (event.type) {
      case EcuEventType::READY_TO_DRIVE_REQUEST:
        readyToDriveRequested = true;
        break;

      case EcuEventType::SHUTDOWN_REQUEST:
        shutdownRequested = true;
        break;
    }
  }
}

/**
 * @brief Verifica que los nodos de seguridad esten presentes y sanos.
 *
 * En la prueba de banco no hay inversor ni BMS en el bus, asi que da
 * por buena la situacion. Pendiente exigir de verdad las flags driver,
 * batteryManagement, accelerator y brake cuando esos nodos existan.
 *
 * @return bool  true si el auto puede pasar de BOOT a CONFIG.
 */
static bool safetyNodesAreHealthy(void) {
  return true;
}

/**
 * @brief Verifica si algun nodo de seguridad fallo durante el arranque.
 *
 * Devuelve false a proposito mientras no haya nodos de seguridad: si
 * devolviera true, el auto entraria en FAULT apenas arranca y quedaria
 * muerto. Pendiente detectar la falla explicita del inversor o del BMS,
 * que es distinto de su ausencia, cubierta por safetyNodesAreHealthy().
 *
 * @return bool  true si hay que ir directo a FAULT.
 */
static bool safetyNodeHasFailed(void) {
  return false;
}

/**
 * @brief Verifica que los nodos presentes terminaron de configurarse.
 *
 * Los nodos de banco no necesitan configuracion: arrancan midiendo y
 * emitiendo solos. Pendiente esperar el acuse de cada nodo cuando
 * exista el mensaje de configuracion.
 *
 * @return bool  true si el auto puede pasar de CONFIG a CAR_READY.
 */
static bool presentNodesAreConfigured(void) {
  return true;
}

/**
 * @brief Verifica que la secuencia de Ready To Drive este completa.
 *
 * Para la prueba alcanza con el boton. Pendiente para el auto: el
 * reglamento exige ademas freno accionado y tractive system activo,
 * mas el sonido de RTD antes de habilitar torque.
 *
 * @return bool  true si el auto puede pasar de CAR_READY a CAR_ON.
 */
static bool readyToDriveSequenceIsComplete(void) {
  return readyToDriveRequested || readyToDriveButtonIsPressed();
}

/**
 * @brief Verifica si se pidio apagar el torque.
 *
 * @return bool  true si el auto debe volver de CAR_ON a CAR_READY.
 */
static bool shutdownWasRequested(void) {
  return shutdownRequested;
}

/**
 * @brief Lee el boton de Ready To Drive con antirrebote.
 *
 * El boton se lee con pull-up interna, asi que en reposo el pin esta en
 * alto y apretado queda en bajo. Se exige que la lectura se mantenga
 * estable un rato antes de darla por buena, porque el contacto mecanico
 * rebota y produciria varias pulsaciones de una sola.
 *
 * @return bool  true en el instante en que el boton pasa a apretado.
 */
static bool readyToDriveButtonIsPressed(void) {
  static bool     lastStableReading = false;
  static bool     lastRawReading = false;
  static uint32_t lastChangeMilliseconds = 0;

  bool rawReading = (digitalRead(READY_TO_DRIVE_BUTTON_PIN) == LOW);

  if (rawReading != lastRawReading) {
    lastRawReading = rawReading;
    lastChangeMilliseconds = millis();
    return false;
  }

  if (millis() - lastChangeMilliseconds < BUTTON_DEBOUNCE_MILLISECONDS) {
    return false;
  }

  /* Solo interesa el flanco: apretado y sostenido no vale como una
     pulsacion nueva en cada vuelta del bucle. */
  bool isNewPress = (rawReading && !lastStableReading);
  lastStableReading = rawReading;
  return isNewPress;
}

/**
 * @brief Registra un WARNING si falta algun nodo informativo.
 *
 * Un nodo informativo caido no impide correr: solo queda asentado para
 * que se vea en la telemetria y en el box.
 *
 * @return void
 */
static void warnAboutMissingInformativeNodes(void) {
  SensorFlags flags = sensorsGetFlags();

  bool someInformativeNodeIsMissing =
      !flags.wheelSpeedFront || !flags.wheelSpeedRear || !flags.motorSpeed ||
      !flags.steeringWheel || !flags.inertialUnit || !flags.tirePressure;

  if (someInformativeNodeIsMissing) {
    /* Pendiente: un codigo distinto por nodo, para poder identificar
       cual falto sin ir a mirar el bus. */
    errorReport(ErrorLevel::WARNING, ERROR_CODE_INFORMATIVE_NODE_MISSING);
  }
}
