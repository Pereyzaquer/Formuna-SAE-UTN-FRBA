/************************************************************
 *  Proyecto : ECU
 *  Archivo  : webmon.cpp
 *  Equipo   : UTN BA Motorsport Formula student team
 *  Fecha    : 14/8/2026
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Monitoreo en vivo del estado de la ECU por wifi. Sirve una
 *  pagina en http://192.168.4.1/ y el mismo dato en JSON en
 *  /api/state, para poder mirar el auto desde el celular sin
 *  desmontar nada.
 *
 *  Hardware:
 *  --------------------------------------------------------
 *  - MCU: ESP32 S3.
 *  - Sensores:
 *
 *  Notas:
 *  --------------------------------------------------------
 *  Es solo lectura: no hay ningun endpoint que cambie el estado
 *  del auto, y no debe haberlo. Cualquiera que se conecte al AP
 *  ve esta pagina, asi que nada que apague o encienda algo puede
 *  vivir aca.
 *
 *  La vida del servidor esta atada a la del wifi (lo levanta y
 *  lo baja taskOta), asi que en CAR_ON no existe.
 *
 ************************************************************/

/************************************************************
 *                     INCLUDES
 ************************************************************/
#include "../include/ECU.h"
#include <WebServer.h>

/************************************************************
 *                VARIABLES GLOBALES
 ************************************************************/
static WebServer server(80);
static bool running = false;

/************************************************************
 *             PROTOTIPOS DE FUNCIONES LOCALES
 ************************************************************/
static void handleRoot(void);
static void handleApiState(void);

/************************************************************
 *                    PAGINA WEB
 ************************************************************/
/* Se guarda en flash y no en RAM: son casi 2 kB que no tiene sentido
   tener en memoria todo el tiempo para servirlos de vez en cuando. */
static const char PAGE[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ECU - UTN BA Motorsport</title>
<style>
 body{font-family:system-ui,sans-serif;background:#111;color:#eee;margin:0;padding:1rem}
 h1{font-size:1rem;color:#888;font-weight:400;margin:0 0 1rem}
 #state{font-size:2.5rem;font-weight:700;margin:0 0 .5rem}
 .grid{display:grid;grid-template-columns:auto 1fr;gap:.3rem 1rem;margin:1rem 0}
 .k{color:#888}
 h2{font-size:.8rem;color:#888;font-weight:400;margin:1.5rem 0 .5rem;
    text-transform:uppercase;letter-spacing:.05em}
 .node{display:flex;justify-content:space-between;padding:.4rem 0;
       border-bottom:1px solid #222}
 .cat{color:#666;font-size:.75rem}
 .ok{color:#4c4}
 .no{color:#c44}
 .stale{opacity:.35}
</style>
</head>
<body>
<h1>ECU &middot; UTN BA Motorsport</h1>
<div id="state">--</div>
<div class="grid">
 <span class="k">Uptime</span><span id="uptime">--</span>
 <span class="k">Falla</span><span id="fault">--</span>
 <span class="k">Tramas CAN</span><span id="rx">--</span>
</div>
<h2>Nodos</h2>
<div id="nodes"></div>
<script>
// Cada nodo con su categoria: B corta el auto, A no. Esta duplicado
// respecto de SensorFlags en ECU.h; si se agrega un sensor hay que
// tocar los dos lados.
const NODES=[["driver","Driver / inversor","B"],["bms","BMS / bateria","B"],
 ["apps","APPS (acelerador)","B"],["bse","BSE (freno)","B"],
 ["rpmFront","RPM delanteras","A"],["rpmRear","RPM traseras","A"],
 ["steering","Volante","A"],["imu","IMU","A"],["tpms","TPMS","A"]];
const COLORS={FAULT:"#c44",BOOT:"#48f",CONFIG:"#cc4",CAR_READY:"#4c4",CAR_ON:"#4c4"};

function draw(d){
 document.body.classList.remove("stale");
 const s=document.getElementById("state");
 s.textContent=d.state; s.style.color=COLORS[d.state]||"#eee";
 document.getElementById("uptime").textContent=(d.uptime/1000).toFixed(1)+" s";
 document.getElementById("fault").textContent=
   d.fault.level+(d.fault.code?" (0x"+d.fault.code.toString(16).padStart(4,"0")+")":"");
 document.getElementById("rx").textContent=d.rx;
 document.getElementById("nodes").innerHTML=NODES.map(([k,label,cat])=>
   '<div class="node"><span>'+label+' <span class="cat">'+cat+'</span></span>'+
   '<span class="'+(d.nodes[k]?"ok":"no")+'">'+(d.nodes[k]?"presente":"ausente")+
   '</span></div>').join("");
}
// Si la ECU deja de responder la pagina se atenua en vez de quedarse
// mostrando datos viejos como si fueran actuales.
function poll(){
 fetch("/api/state").then(r=>r.json()).then(draw)
  .catch(()=>document.body.classList.add("stale"));
}
poll(); setInterval(poll,500);
</script>
</body>
</html>)HTML";

/**
 * @brief Levanta el servidor web de monitoreo en el puerto 80.
 *
 * @return void
 */
void webmonBegin(void) {
  if (running) {
    return;
  }
  server.on("/", handleRoot);
  server.on("/api/state", handleApiState);
  server.begin();
  running = true;
}

/**
 * @brief Baja el servidor web de monitoreo.
 *
 * @return void
 */
void webmonStop(void) {
  if (!running) {
    return;
  }
  server.stop();
  running = false;
}

/**
 * @brief Atiende las peticiones web pendientes.
 *
 * @return void
 */
void webmonHandle(void) {
  if (running) {
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
static void handleRoot(void) {
  server.send_P(200, "text/html", PAGE);
}

/**
 * @brief Sirve el estado completo de la ECU en JSON.
 *
 * @return void
 */
static void handleApiState(void) {
  SensorFlags f = sensorsGetFlags();
  char buf[512];

  snprintf(buf, sizeof(buf),
    "{\"state\":\"%s\",\"uptime\":%lu,"
    "\"fault\":{\"level\":\"%s\",\"code\":%u},\"rx\":%lu,"
    "\"nodes\":{\"driver\":%d,\"bms\":%d,\"apps\":%d,\"bse\":%d,"
    "\"rpmFront\":%d,\"rpmRear\":%d,\"steering\":%d,\"imu\":%d,\"tpms\":%d}}",
    stateName(stateGet()), millis(),
    faultLevelName(faultGetLevel()), faultGetCode(), canGetRxCount(),
    f.driver, f.bms, f.apps, f.bse,
    f.rpmFront, f.rpmRear, f.steering, f.imu, f.tpms);

  server.send(200, "application/json", buf);
}
