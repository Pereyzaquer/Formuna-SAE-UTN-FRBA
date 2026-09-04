/************************************************************
 *  Proyecto : ECU
 *  Archivo  : webmonitor.cpp
 *  Equipo   : UTN BA Motorsport Formula student team
 *  Fecha    : 2/9/2026
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Monitoreo por wifi. Sirve tres cosas en el punto de acceso
 *  de la ECU:
 *
 *    /              pagina de estado en vivo
 *    /api/estado    el mismo dato en JSON, para la pagina
 *    /registro.csv  la grabacion de la ultima ejecucion
 *    /api/rtd       arranca la ejecucion, reemplaza al boton fisico
 *    /api/detener   corta la ejecucion antes de tiempo
 *
 *  Hardware:
 *  --------------------------------------------------------
 *  - MCU: ESP32 S3.
 *  - Sensores: ninguno propio.
 *
 *  Notas:
 *  --------------------------------------------------------
 *  ATENCION: este servidor era de solo lectura y dejo de serlo.
 *  Los endpoints /api/rtd y /api/detener cambian el estado del
 *  vehiculo, y cualquiera que se conecte al punto de acceso
 *  llega a ellos sin contraseña.
 *
 *  Se agregaron porque para la prueba de banco no hay boton
 *  fisico de Ready To Drive, y en el banco entrar en CAR_ON no
 *  mueve nada: solo arranca el registrador. En el auto, con el
 *  inversor conectado, ese mismo estado habilita torque, y un
 *  boton de arranque accesible por wifi sin autenticacion seria
 *  inaceptable.
 *
 *  Antes de montar la ECU en el auto hay que poner en false la
 *  constante WEB_CONTROL_ENABLED_FOR_BENCH_TEST. Con eso los
 *  endpoints devuelven 403 y los botones desaparecen de la
 *  pagina, sin tener que borrar codigo.
 *
 *  El resto de la pagina si es de solo lectura.
 *
 *  El servidor se levanta y se baja junto con el wifi, desde
 *  taskWifi.
 *
 ************************************************************/

/************************************************************
 *                     INCLUDES
 ************************************************************/
#include "../include/ECU.h"
#include <WebServer.h>

/************************************************************
 *               CONSTANTES DEL SISTEMA
 ************************************************************/

/**
 * Botones de arranque y parada por wifi, para la prueba de banco.
 *
 * Poner en false antes de montar la ECU en el auto: ver la advertencia
 * del encabezado de este archivo.
 */
static const bool WEB_CONTROL_ENABLED_FOR_BENCH_TEST = true;

/************************************************************
 *                VARIABLES GLOBALES
 ************************************************************/
static WebServer server(80);
static bool serverIsRunning = false;

/************************************************************
 *             PROTOTIPOS DE FUNCIONES LOCALES
 ************************************************************/
static void handlePageRequest(void);
static void handleStateRequest(void);
static void handleCsvRequest(void);
static void handleReadyToDriveRequest(void);
static void handleStopRequest(void);
static bool sendEventToStateMachine(EcuEventType type);

/************************************************************
 *                    PAGINA WEB
 ************************************************************/
/* Se guarda en flash y no en memoria RAM: son un par de kB que no vale
   la pena tener cargados todo el tiempo para servirlos de vez en cuando. */
static const char PAGE[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ECU - UTN BA Motorsport</title>
<style>
 body{font-family:system-ui,sans-serif;background:#111;color:#eee;margin:0;padding:1rem}
 h1{font-size:1rem;color:#888;font-weight:400;margin:0 0 1rem}
 #estado{font-size:2.5rem;font-weight:700;margin:0 0 .5rem}
 .grid{display:grid;grid-template-columns:auto 1fr;gap:.3rem 1rem;margin:1rem 0}
 .clave{color:#888}
 h2{font-size:.8rem;color:#888;font-weight:400;margin:1.5rem 0 .5rem;
    text-transform:uppercase;letter-spacing:.05em}
 .fila{display:flex;justify-content:space-between;padding:.4rem 0;
       border-bottom:1px solid #222}
 .presente{color:#4c4}
 .ausente{color:#c44}
 .barra{height:6px;background:#222;border-radius:3px;overflow:hidden;margin:.5rem 0}
 .barra div{height:100%;background:#4c4;width:0%}
 a{color:#48f}
 button{font:inherit;padding:.6rem 1rem;margin-right:.5rem;border:0;
        border-radius:4px;background:#2a4;color:#fff;cursor:pointer}
 button:disabled{background:#333;color:#666;cursor:not-allowed}
 #detener{background:#a33}
 .sinconexion{opacity:.35}
</style>
</head>
<body>
<h1>ECU &middot; UTN BA Motorsport</h1>
<div id="estado">--</div>
<div class="grid">
 <span class="clave">Encendida hace</span><span id="uptime">--</span>
 <span class="clave">Falla</span><span id="falla">--</span>
 <span class="clave">Tramas CAN</span><span id="tramas">--</span>
 <span class="clave">Nodos presentes</span><span id="nodos">--</span>
 <span class="clave">Bus caido</span><span id="recuperaciones">--</span>
</div>

<h2>Grabacion</h2>
<div class="barra"><div id="avance"></div></div>
<div class="grid">
 <span class="clave">Estado</span><span id="grabando">--</span>
 <span class="clave">Muestras</span><span id="muestras">--</span>
</div>
<p id="controles">
 <button id="arrancar" onclick="mandar('/api/rtd')">Arrancar ejecucion</button>
 <button id="detener" onclick="mandar('/api/detener')">Detener</button>
</p>
<p><a href="/registro.csv" download>Descargar registro.csv</a></p>

<h2>Canales</h2>
<div id="canales"></div>

<script>
const COLORES={FAULT:"#c44",BOOT:"#48f",CONFIG:"#cc4",CAR_READY:"#4c4",CAR_ON:"#4c4"};

function dibujar(datos){
 document.body.classList.remove("sinconexion");

 const estado=document.getElementById("estado");
 estado.textContent=datos.estado;
 estado.style.color=COLORES[datos.estado]||"#eee";

 document.getElementById("uptime").textContent=(datos.uptime/1000).toFixed(1)+" s";
 document.getElementById("falla").textContent=datos.falla.nivel+
   (datos.falla.codigo?" (0x"+datos.falla.codigo.toString(16).padStart(4,"0")+")":"");
 document.getElementById("tramas").textContent=datos.tramas;
 document.getElementById("nodos").textContent=datos.nodos;

 // Si este contador sube, la ECU esta sola en el bus o el cableado esta
 // mal: nadie le confirma lo que transmite. Se resalta porque un numero
 // creciendo aca explica casi cualquier sintoma raro del bus.
 const recuperaciones=document.getElementById("recuperaciones");
 recuperaciones.textContent=datos.recuperaciones+" veces";
 recuperaciones.className=datos.recuperaciones?"ausente":"presente";

 document.getElementById("grabando").textContent=
   datos.grabando?"en curso":(datos.muestras?"terminada":"sin datos");
 document.getElementById("muestras").textContent=datos.muestras;
 document.getElementById("avance").style.width=datos.avance+"%";

 // El boton de arranque solo sirve en CAR_READY, y el de parada solo
 // durante una ejecucion. Deshabilitarlos evita mandar ordenes que la
 // maquina de estados va a ignorar, que desde afuera parece una falla.
 document.getElementById("controles").style.display=
   datos.control?"block":"none";
 document.getElementById("arrancar").disabled=(datos.estado!=="CAR_READY");
 document.getElementById("detener").disabled=!datos.grabando;

 document.getElementById("canales").innerHTML=datos.canales.map(canal=>
  '<div class="fila"><span>'+canal.nombre+'</span><span class="'+
  (canal.valido?"presente":"ausente")+'">'+
  (canal.valido?canal.valor.toFixed(1):"sin datos")+'</span></div>').join("");
}

// Despues de mandar una orden se refresca enseguida, sin esperar al
// proximo refresco automatico, para que el boton se sienta inmediato.
function mandar(ruta){
 fetch(ruta,{method:"POST"}).then(consultar);
}

// Si la ECU deja de contestar, la pagina se atenua en vez de seguir
// mostrando numeros viejos como si fueran actuales.
function consultar(){
 fetch("/api/estado").then(respuesta=>respuesta.json()).then(dibujar)
  .catch(()=>document.body.classList.add("sinconexion"));
}
consultar(); setInterval(consultar,500);
</script>
</body>
</html>)HTML";

/**
 * @brief Levanta el servidor web de monitoreo en el puerto 80.
 *
 * @return void
 */
void webMonitorStart(void) {
  if (serverIsRunning) {
    return;
  }

  server.on("/", handlePageRequest);
  server.on("/api/estado", handleStateRequest);
  server.on("/registro.csv", handleCsvRequest);

  /* Se registran siempre, aunque el control este apagado: asi contestan
     403 con un motivo en vez de un 404 que parece un error de tipeo.
     Van por POST y no por GET porque cambian el estado del vehiculo, y
     un GET lo dispara cualquier cosa que precargue enlaces. */
  server.on("/api/rtd", HTTP_POST, handleReadyToDriveRequest);
  server.on("/api/detener", HTTP_POST, handleStopRequest);

  server.begin();

  serverIsRunning = true;
}

/**
 * @brief Baja el servidor web de monitoreo.
 *
 * @return void
 */
void webMonitorStop(void) {
  if (!serverIsRunning) {
    return;
  }

  server.stop();
  serverIsRunning = false;
}

/**
 * @brief Atiende las peticiones web pendientes.
 *
 * @return void
 */
void webMonitorHandleRequests(void) {
  if (serverIsRunning) {
    server.handleClient();
  }
}

/************************************************************
 *                  FUNCIONES LOCALES
 ************************************************************/

/**
 * @brief Sirve la pagina de monitoreo.
 *
 * @return void
 */
static void handlePageRequest(void) {
  server.send_P(200, "text/html", PAGE);
}

/**
 * @brief Sirve el estado completo de la ECU en JSON.
 *
 * @return void
 */
static void handleStateRequest(void) {
  /* Duracion de la ejecucion expresada en porcentaje, para la barra de
     avance. Se calcula aca y no en la pagina para no repetir el dato de
     cuanto dura una ejecucion en dos lugares. */
  uint32_t elapsed = loggerGetElapsedMilliseconds();
  uint32_t progressPercent = loggerIsRecording() ? (elapsed / 900) : 100;
  if (progressPercent > 100) {
    progressPercent = 100;
  }
  if (loggerGetSampleCount() == 0) {
    progressPercent = 0;
  }

  String json = "{";
  json += "\"estado\":\"" + String(stateGetName(stateGet())) + "\",";
  json += "\"uptime\":" + String(millis()) + ",";
  json += "\"falla\":{\"nivel\":\"" + String(errorGetLevelName(errorGetLevel())) +
          "\",\"codigo\":" + String(errorGetCode()) + "},";
  json += "\"tramas\":" + String(canGetReceivedFrameCount()) + ",";
  json += "\"recuperaciones\":" + String(canGetBusRecoveryCount()) + ",";
  json += "\"nodos\":" + String(sensorsCountPresentNodes()) + ",";
  json += "\"grabando\":" + String(loggerIsRecording() ? "true" : "false") + ",";
  json += "\"muestras\":" + String(loggerGetSampleCount()) + ",";
  json += "\"avance\":" + String(progressPercent) + ",";
  json += "\"control\":" +
          String(WEB_CONTROL_ENABLED_FOR_BENCH_TEST ? "true" : "false") + ",";
  json += "\"canales\":[";

  for (uint8_t index = 0; index < static_cast<uint8_t>(LogChannel::COUNT);
       index++) {
    LogChannel channel = static_cast<LogChannel>(index);
    float value = 0.0f;
    bool  valueIsValid = loggerGetCurrentValue(channel, &value);

    if (index > 0) {
      json += ",";
    }
    json += "{\"nombre\":\"" + String(loggerGetChannelName(channel)) + "\",";
    json += "\"valido\":" + String(valueIsValid ? "true" : "false") + ",";
    json += "\"valor\":" + String(value, 2) + "}";
  }

  json += "]}";

  server.send(200, "application/json", json);
}

/**
 * @brief Encola un evento para la maquina de estados.
 *
 * Los endpoints de control no tocan el estado del vehiculo: mandan el
 * mismo evento que mandaria el boton fisico y dejan que la maquina de
 * estados decida. Asi hay un unico lugar donde se decide en que estado
 * esta el auto, y las ordenes que no correspondan al estado actual se
 * ignoran solas.
 *
 * @param[in]  type  Tipo de evento a encolar.
 *
 * @return bool  false si la cola estaba llena.
 */
static bool sendEventToStateMachine(EcuEventType type) {
  EcuEvent event;
  event.type = type;
  event.data = 0;

  return (xQueueSend(queueEvents, &event, 0) == pdTRUE);
}

/**
 * @brief Arranca la ejecucion, en reemplazo del boton fisico de RTD.
 *
 * @return void
 */
static void handleReadyToDriveRequest(void) {
  if (!WEB_CONTROL_ENABLED_FOR_BENCH_TEST) {
    server.send(403, "application/json",
                "{\"error\":\"control por wifi deshabilitado\"}");
    return;
  }

  if (!sendEventToStateMachine(EcuEventType::READY_TO_DRIVE_REQUEST)) {
    server.send(503, "application/json",
                "{\"error\":\"cola de eventos llena\"}");
    return;
  }

  /* La respuesta confirma que la orden se encolo, no que el auto haya
     arrancado: eso lo decide la maquina de estados y se ve en el estado
     que devuelve /api/estado un instante despues. */
  server.send(200, "application/json", "{\"encolado\":\"rtd\"}");
}

/**
 * @brief Corta la ejecucion antes de que se cumplan los 90 segundos.
 *
 * @return void
 */
static void handleStopRequest(void) {
  if (!WEB_CONTROL_ENABLED_FOR_BENCH_TEST) {
    server.send(403, "application/json",
                "{\"error\":\"control por wifi deshabilitado\"}");
    return;
  }

  if (!sendEventToStateMachine(EcuEventType::SHUTDOWN_REQUEST)) {
    server.send(503, "application/json",
                "{\"error\":\"cola de eventos llena\"}");
    return;
  }

  server.send(200, "application/json", "{\"encolado\":\"detener\"}");
}

/**
 * @brief Sirve la grabacion como archivo CSV.
 *
 * El archivo sale con una columna por canal, tenga datos o no, para que
 * la planilla siempre tenga la misma forma. Cada fila corresponde a una
 * muestra y solo lleva valor en la columna del canal que hablo en ese
 * instante; las demas quedan vacias. Una celda vacia es un hueco en el
 * grafico, que es lo correcto: si se rellenara con cero, el grafico
 * mostraria una medicion que nunca existio.
 *
 * Se manda por partes en lugar de armar todo el texto en memoria: una
 * ejecucion completa son cientos de kB y no entran comodos en RAM.
 *
 * @return void
 */
static void handleCsvRequest(void) {
  const uint8_t channelCount = static_cast<uint8_t>(LogChannel::COUNT);

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/csv", "");

  /* Fila de encabezado con el nombre de cada columna. */
  String header = "tiempo_s";
  for (uint8_t index = 0; index < channelCount; index++) {
    header += ",";
    header += loggerGetChannelName(static_cast<LogChannel>(index));
  }
  header += "\n";
  server.sendContent(header);

  /* Se acumulan varias filas antes de mandarlas: una llamada de red por
     fila haria la descarga lentisima. */
  String block;
  block.reserve(2048);

  uint32_t sampleCount = loggerGetSampleCount();

  for (uint32_t sampleIndex = 0; sampleIndex < sampleCount; sampleIndex++) {
    uint32_t   timeMilliseconds = 0;
    LogChannel channel = LogChannel::COUNT;
    float      value = 0.0f;

    if (!loggerGetSample(sampleIndex, &timeMilliseconds, &channel, &value)) {
      break;
    }

    block += String(timeMilliseconds / 1000.0f, 3);

    for (uint8_t index = 0; index < channelCount; index++) {
      block += ",";
      if (index == static_cast<uint8_t>(channel)) {
        block += String(value, 2);
      }
    }
    block += "\n";

    if (block.length() > 1536) {
      server.sendContent(block);
      block = "";
    }
  }

  if (block.length() > 0) {
    server.sendContent(block);
  }

  /* Un bloque vacio le avisa al navegador que el archivo termino. */
  server.sendContent("");
}
