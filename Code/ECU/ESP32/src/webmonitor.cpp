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
 *    /api/rtd       arranca la ejecucion, reemplaza al boton fisico;
 *                   acepta duracion=N (segundos) para esa ejecucion
 *    /api/detener   corta la ejecucion antes de tiempo
 *    /api/nodos     lista de nodos vistos, con su nombre
 *    /api/nombre    guarda el nombre de un nodo (id y nombre)
 *    /api/pantalla  ajusta la pantalla: canal y maximo de la barra
 *                   de RPM, y maximo de la barra de freno
 *    /api/prueba    mueve las barras de la pantalla con valores de
 *                   prueba (acelerador, freno, rpm); fin=1 termina
 *    /api/bms       elige la trama del BMS: id, byte y escala
 *    /api/can       guarda la velocidad del bus (kbps) y reinicia
 *    /api/calibrar  fija reposo o a fondo de un pedal (sensor, punto)
 *    /api/chequeos  activa o apaga bateria, shutdown y pedales
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
static void handleNodesRequest(void);
static void handleNameRequest(void);
static void handleDisplayRequest(void);
static void handleTestRequest(void);
static void handleBatteryRequest(void);
static void handleBitrateRequest(void);
static void handleCalibrationRequest(void);
static void handleChecksRequest(void);
static void appendNodeJson(String &json, uint32_t identifier, bool known,
                           uint32_t frameCount);
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
 :root{--fondo:#0f1115;--tarjeta:#171a21;--borde:#262a33;--texto:#e8eaf0;
       --tenue:#8b91a0;--ok:#3ecf6f;--mal:#ff5d5d;--aviso:#ffb454;--info:#4f8cff;
       --radio:12px}
 *{box-sizing:border-box}
 body{font-family:system-ui,-apple-system,"Segoe UI",sans-serif;background:var(--fondo);
      color:var(--texto);margin:0;padding:1rem;max-width:960px;margin-inline:auto;
      line-height:1.4}
 header{display:flex;align-items:center;justify-content:space-between;gap:1rem;
        margin-bottom:1rem}
 h1{font-size:.85rem;color:var(--tenue);font-weight:500;margin:0;letter-spacing:.04em;
    text-transform:uppercase}
 #estado{display:inline-flex;align-items:center;gap:.5rem;font-size:1.6rem;
         font-weight:700;padding:.3rem .9rem;border-radius:999px;
         background:var(--tarjeta);border:1px solid var(--borde)}
 #estado::before{content:"";width:.7rem;height:.7rem;border-radius:50%;
                 background:currentColor;box-shadow:0 0 10px currentColor}
 .tarjeta{background:var(--tarjeta);border:1px solid var(--borde);
          border-radius:var(--radio);padding:1rem;margin-bottom:1rem}
 h2{font-size:.75rem;color:var(--tenue);font-weight:600;margin:0 0 .8rem;
    text-transform:uppercase;letter-spacing:.06em}
 .kpis{display:grid;grid-template-columns:repeat(3,1fr);gap:.6rem}
 .kpi{background:var(--fondo);border:1px solid var(--borde);border-radius:8px;
      padding:.6rem .8rem}
 .kpi .clave{display:block;font-size:.72rem;color:var(--tenue);margin-bottom:.15rem}
 .kpi span:last-child{font-size:1.15rem;font-weight:600;
                      font-variant-numeric:tabular-nums}
 .presente{color:var(--ok)} .ausente{color:var(--mal)}
 .nuevo{color:var(--aviso)} .callado{color:var(--mal)}
 .aviso{color:var(--aviso)}
 .barra{height:8px;background:var(--fondo);border-radius:4px;overflow:hidden;
        margin:.6rem 0}
 .barra div{height:100%;background:var(--ok);width:0%;transition:width .4s}
 .grabacion{display:flex;gap:1.2rem;flex-wrap:wrap;color:var(--tenue);font-size:.9rem}
 .grabacion b{color:var(--texto);font-weight:600;font-variant-numeric:tabular-nums}
 #controles{display:flex;flex-wrap:wrap;gap:.6rem;align-items:center;margin-top:1rem}
 label{color:var(--tenue);font-size:.9rem;display:flex;align-items:center;gap:.4rem}
 input{font:inherit;background:var(--fondo);color:var(--texto);
       border:1px solid var(--borde);border-radius:8px;padding:.45rem .6rem;min-width:0}
 input:focus{outline:2px solid var(--info);outline-offset:0;border-color:transparent}
 button{font:inherit;font-weight:600;padding:.55rem 1.1rem;border:0;border-radius:8px;
        background:var(--ok);color:#06210f;cursor:pointer}
 button:hover:not(:disabled){filter:brightness(1.1)}
 button:disabled{background:var(--borde);color:var(--tenue);cursor:not-allowed}
 #detener{background:transparent;color:var(--mal);border:1px solid var(--mal)}
 #detener:disabled{border-color:var(--borde);color:var(--tenue)}
 .secundario{background:transparent;color:var(--info);border:1px solid var(--borde);
             padding:.35rem .8rem;font-weight:500}
 a.descarga{display:inline-block;margin-top:.8rem;color:var(--info);text-decoration:none;
            font-size:.9rem}
 a.descarga:hover{text-decoration:underline}
 #alerta{background:var(--mal);color:#fff;padding:.9rem 1rem;border-radius:var(--radio);
         margin:0 0 1rem;font-weight:600}
 #alerta div{font-weight:400;opacity:.9;margin-top:.2rem;font-size:.9rem}
 .sinconexion{opacity:.35;transition:opacity .5s}
 .lista{display:grid;grid-template-columns:1fr auto;gap:0 1rem}
 .lista span{padding:.45rem 0;border-bottom:1px solid var(--borde);font-size:.95rem}
 .lista span:nth-child(even){text-align:right;font-variant-numeric:tabular-nums;
                              font-weight:600}
 .lista span:nth-last-child(-n+2){border-bottom:0}
 .nodo{display:grid;grid-template-columns:4.5em 1fr auto auto;gap:.6rem;
       align-items:center;padding:.45rem 0;border-bottom:1px solid var(--borde)}
 .nodo:last-child{border-bottom:0}
 .nodo code{color:var(--tenue);font-size:.85rem}
 .nodo .etiqueta{font-size:.85rem;white-space:nowrap}
 .ajustes{display:flex;flex-wrap:wrap;gap:.6rem;align-items:center}
 .nota-chica{color:var(--tenue);font-size:.85rem;margin:.6rem 0 0}
 .pedal{display:flex;gap:.5rem;align-items:center;justify-content:flex-end;flex-wrap:wrap}
 .deslizables{display:grid;gap:.6rem;margin-top:.8rem}
 .deslizables label{display:grid;grid-template-columns:6.5em 1fr 4em auto;gap:.6rem}
 .deslizables b{text-align:right;font-variant-numeric:tabular-nums;color:var(--texto)}
 input[type=range]{accent-color:var(--ok);padding:0}
 select{font:inherit;background:var(--fondo);color:var(--texto);
        border:1px solid var(--borde);border-radius:8px;padding:.45rem .6rem}
 @media (max-width:520px){
  #estado{font-size:1.2rem}
  .kpis{grid-template-columns:repeat(2,1fr)}
  .nodo{grid-template-columns:4em 1fr;grid-auto-flow:row}
  .nodo .etiqueta,.nodo button{grid-column:2}
 }
</style>
</head>
<body>
<header>
 <h1>ECU &middot; UTN BA Motorsport</h1>
 <div id="estado">--</div>
</header>

<div id="alerta" hidden>
 FALLA - la ECU esta detenida
 <div id="alertaDetalle"></div>
</div>

<section class="tarjeta">
 <h2>Estado</h2>
 <div class="kpis">
  <div class="kpi"><span class="clave">Encendida hace</span><span id="uptime">--</span></div>
  <div class="kpi"><span class="clave">Falla</span><span id="falla">--</span></div>
  <div class="kpi"><span class="clave">Tramas CAN</span><span id="tramas">--</span></div>
  <div class="kpi"><span class="clave">Nodos vistos</span><span id="nodos">--</span></div>
  <div class="kpi"><span class="clave">Bus caido</span><span id="recuperaciones">--</span></div>
  <div class="kpi"><span class="clave">Ruedas midiendo</span><span id="ruedas">--</span></div>
 </div>
</section>

<section class="tarjeta">
 <h2>Grabacion</h2>
 <div class="barra"><div id="avance"></div></div>
 <div class="grabacion">
  <span>Estado <b id="grabando">--</b></span>
  <span>Muestras <b id="muestras">--</b></span>
  <span>Transcurrido <b id="transcurrido">--</b></span>
 </div>
 <div id="controles">
  <label>Duracion <input id="duracion" type="number" min="1" max="180" step="1" style="width:5em"> s</label>
  <button id="arrancar" onclick="arrancar()">Ready to move</button>
  <button id="detener" onclick="mandar('/api/detener')">Detener</button>
 </div>
 <a class="descarga" href="/registro.csv" download>&#8595; Descargar registro.csv</a>
</section>

<section class="tarjeta">
 <h2>Canales</h2>
 <div id="canales" class="lista"></div>
</section>

<section class="tarjeta">
 <h2>Pantalla</h2>
 <div id="pantalla" class="ajustes">
  <label>Barra de RPM <select id="rpmCanal"></select></label>
  <label>llena en <input id="rpmMaximo" type="number" min="1" step="any" style="width:6em"></label>
  <label>Freno llena en <input id="frenoMaximo" type="number" min="1" step="any" style="width:5em"> bar</label>
  <button class="secundario" onclick="guardarPantalla()">Aplicar</button>
 </div>
</section>

<section class="tarjeta">
 <h2>Pedales y shutdown</h2>
 <div class="grabacion">
  <span>Shutdown <b id="sdc">--</b></span>
  <span>Acelerador <b id="acelerador">--</b></span>
  <span>Diferencia APPS <b id="diferencia">--</b></span>
 </div>
 <div id="pedales" class="lista" style="margin-top:.6rem"></div>
 <p class="nota-chica">Calibrar: con el pedal suelto, "Reposo"; pisado a fondo, "A fondo". Se guarda en la ECU.</p>
 <div class="ajustes" style="margin-top:.8rem">
  <label><input type="checkbox" id="chkBateria" onchange="guardarChequeos()"> Exigir bateria para READY</label>
  <label><input type="checkbox" id="chkShutdown" onchange="guardarChequeos()"> Vigilar shutdown</label>
  <label><input type="checkbox" id="chkPedales" onchange="guardarChequeos()"> Vigilar APPS y freno</label>
 </div>
</section>

<section class="tarjeta">
 <h2>Bateria (BMS)</h2>
 <div class="grabacion"><span>Carga <b id="bateria">--</b></span><span>Ultima trama <b id="bmsTrama">sin recibir</b></span></div>
 <div class="ajustes" style="margin-top:.8rem">
  <label>ID <input id="bmsId" style="width:5em" placeholder="0x6B0"></label>
  <label>byte <input id="bmsByte" type="number" min="0" max="7" style="width:4em"></label>
  <label>% por cuenta <input id="bmsEscala" type="number" step="any" min="0" style="width:5em"></label>
  <button class="secundario" onclick="guardarBms()">Aplicar</button>
 </div>
 <div class="ajustes" style="margin-top:.8rem">
  <label>Velocidad del bus <select id="kbps"><option>125</option><option>250</option><option>500</option><option>1000</option></select> kbps</label>
  <button class="secundario" onclick="guardarKbps()">Guardar y reiniciar</button>
 </div>
</section>

<section class="tarjeta">
 <h2>Prueba de barras</h2>
 <label><input id="probar" type="checkbox" onchange="cambiarPrueba()"> Mover la pantalla con los deslizables</label>
 <div class="deslizables">
  <label>Acelerador <input id="pruebaAcelerador" type="range" min="0" max="100" value="0" step="any" oninput="mandarPrueba()"><b id="valorAcelerador">0</b> %</label>
  <label>Freno <input id="pruebaFreno" type="range" min="0" max="100" value="0" step="any" oninput="mandarPrueba()"><b id="valorFreno">0</b> bar</label>
  <label>RPM <input id="pruebaRpm" type="range" min="0" max="1000" value="0" step="any" oninput="mandarPrueba()"><b id="valorRpm">0</b></label>
 </div>
</section>

<section class="tarjeta">
 <h2>Nodos</h2>
 <div id="nodos-lista"></div>
</section>

<script>
const COLORES={FAULT:"var(--mal)",BOOT:"var(--info)",CONFIG:"var(--aviso)",
               CAR_READY:"var(--ok)",CAR_ON:"var(--ok)"};
const NIVELES={NONE:"presente",WARNING:"aviso",CRITICAL:"ausente"};

function dibujar(datos){
 document.body.classList.remove("sinconexion");
 dibujarPedales(datos.pedales);
 dibujarBms(datos.bms);

 const estado=document.getElementById("estado");
 estado.textContent=datos.estado;
 estado.style.color=COLORES[datos.estado]||"var(--texto)";

 document.getElementById("uptime").textContent=(datos.uptime/1000).toFixed(0)+" s";
 const falla=document.getElementById("falla");
 falla.textContent=datos.falla.codigo?datos.falla.texto:"sin avisos";
 falla.title=datos.falla.nivel+" 0x"+datos.falla.codigo.toString(16).padStart(4,"0");
 falla.className=NIVELES[datos.falla.nivel]||"";
 document.getElementById("tramas").textContent=datos.tramas;
 document.getElementById("nodos").textContent=datos.nodos;

 // Si este contador sube, la ECU esta sola en el bus o el cableado esta
 // mal: nadie le confirma lo que transmite. Se resalta porque un numero
 // creciendo aca explica casi cualquier sintoma raro del bus.
 const recuperaciones=document.getElementById("recuperaciones");
 recuperaciones.textContent=datos.recuperaciones+" veces";
 recuperaciones.className=datos.recuperaciones?"ausente":"presente";

 // La alerta de falla va arriba de todo y ademas cambia el titulo de
 // la pestaña: si el navegador esta en segundo plano, es la unica
 // forma de enterarse sin mirar la pagina.
 const enFalla=datos.estado==="FAULT";
 document.getElementById("alerta").hidden=!enFalla;
 document.title=enFalla?"FALLA - ECU":"ECU - UTN BA Motorsport";
 if(enFalla){
  document.getElementById("alertaDetalle").textContent=
    "Codigo "+datos.falla.nivel+" 0x"+datos.falla.codigo.toString(16).padStart(4,"0")+
    ". Se rearma sola si la causa desaparece; si no, reiniciar la ECU.";
 }

 const ruedas=document.getElementById("ruedas");
 ruedas.textContent=datos.ruedas+" de 4";
 ruedas.className=datos.ruedas?"presente":"ausente";

 document.getElementById("grabando").textContent=
   datos.grabando?"en curso":(datos.muestras?"terminada":"sin datos");
 document.getElementById("muestras").textContent=datos.muestras;
 document.getElementById("avance").style.width=datos.avance+"%";
 document.getElementById("transcurrido").textContent=
   datos.grabando?Math.round(datos.avance*datos.duracion/100)+" / "+datos.duracion+" s"
                 :datos.duracion+" s";

 // El campo de duracion muestra lo que tiene la ECU, salvo mientras el
 // usuario lo esta editando: pisarselo debajo de los dedos es molesto.
 const duracion=document.getElementById("duracion");
 if(document.activeElement!==duracion) duracion.value=datos.duracion;

 // El boton de arranque solo sirve en CAR_READY, y el de parada solo
 // durante una ejecucion. Deshabilitarlos evita mandar ordenes que la
 // maquina de estados va a ignorar, que desde afuera parece una falla.
 document.getElementById("controles").style.display=datos.control?"flex":"none";
 document.getElementById("arrancar").disabled=(datos.estado!=="CAR_READY");
 document.getElementById("detener").disabled=!datos.grabando;

 document.getElementById("canales").innerHTML=datos.canales.map(canal=>
  '<span>'+canal.nombre+'</span><span class="'+(canal.valido?"presente":"ausente")+'">'+
  (canal.valido?canal.valor.toFixed(1):"sin datos")+'</span>').join("");
}

// Los nodos se piden aparte del estado: cambian poco y la lista trae
// nombres, que no hace falta recargar cada segundo. Se vuelve a pedir
// al guardar un nombre y cada 5 segundos por si aparece un nodo nuevo.
function consultarNodos(){
 fetch("/api/nodos").then(r=>r.json()).then(nodos=>{
  const activo=document.activeElement;
  const editando=activo&&activo.dataset&&activo.dataset.id;
  document.getElementById("nodos-lista").innerHTML=nodos.map(n=>{
   const estado=n.conocido
     ?(n.visto?(n.callado?'<span class="etiqueta callado">&#9679; callado</span>'
                        :'<span class="etiqueta presente">&#9679; presente</span>')
              :'<span class="etiqueta" style="color:var(--tenue)">&#9675; nunca visto</span>')
     :'<span class="etiqueta nuevo">&#9679; nuevo &middot; '+n.tramas+' tramas</span>';
   return '<div class="nodo"><code>0x'+n.id.toString(16).toUpperCase().padStart(3,"0")+
     '</code><input data-id="'+n.id+'" value="'+n.nombre.replace(/"/g,"&quot;")+
     '" maxlength="24" placeholder="nombre">'+estado+
     '<button class="secundario" onclick="guardarNombre('+n.id+')">Guardar</button></div>';
  }).join("");
  // Si el usuario estaba escribiendo, se le devuelve el foco al mismo
  // campo para no interrumpirlo.
  if(editando){const e=document.querySelector('input[data-id="'+editando+'"]');
   if(e){e.value=activo.value;e.focus();}}
 }).catch(()=>{});
}

// Los ajustes de la pantalla se cargan una sola vez, al abrir la pagina:
// si se refrescaran solos le pisarian el valor a quien esta escribiendo.
// La tarjeta del BMS: valores en vivo en cada consulta; los campos de
// configuracion se cargan una sola vez, junto con los de la pantalla.
function dibujarBms(bms){
 document.getElementById("bateria").textContent=
   bms.bateria===null?"--":bms.bateria.toFixed(1)+" %";
 document.getElementById("bmsTrama").textContent=bms.bytes===null?"sin recibir":
   "0x"+bms.id.toString(16).toUpperCase()+"  ["+bms.bytes+"]  hace "+bms.edad+" ms";
}

function guardarBms(){
 const v=id=>encodeURIComponent(document.getElementById(id).value);
 fetch("/api/bms?id="+v("bmsId")+"&byte="+v("bmsByte")+"&escala="+v("bmsEscala"),
       {method:"POST"});
}

function guardarKbps(){
 if(!confirm("Todos los nodos del bus tienen que usar la misma velocidad. Guardar y reiniciar la ECU?")) return;
 fetch("/api/can?kbps="+document.getElementById("kbps").value,{method:"POST"});
}

// Pedales: lectura en vivo en cada consulta. Cada fila muestra tension,
// por ciento y calibracion, con los dos botones para calibrar.
function dibujarPedales(p){
 const sdc=document.getElementById("sdc");
 sdc.textContent=p.sdc?"abierto":"cerrado";
 sdc.className=p.sdc?"ausente":"presente";
 document.getElementById("acelerador").textContent=p.acelerador.toFixed(0)+" %";
 document.getElementById("diferencia").textContent=p.diferencia.toFixed(1)+" %";
 document.getElementById("pedales").innerHTML=p.sensores.map((s,i)=>
  '<span>'+s.nombre+' <small style="color:var(--tenue)">('+s.reposo.toFixed(2)+' a '+
  s.fondo.toFixed(2)+' V)</small></span><span class="pedal"><b class="'+(s.ok?"presente":"ausente")+'">'+
  s.v.toFixed(2)+' V &middot; '+s.pct.toFixed(0)+' %</b>'+
  '<button class="secundario" onclick="calibrar('+i+',0)">Reposo</button>'+
  '<button class="secundario" onclick="calibrar('+i+',1)">A fondo</button></span>').join("");
}

function calibrar(sensor,fondo){
 fetch("/api/calibrar?sensor="+sensor+"&punto="+(fondo?"fondo":"reposo"),{method:"POST"});
}

function guardarChequeos(){
 const c=id=>document.getElementById(id).checked?1:0;
 fetch("/api/chequeos?bateria="+c("chkBateria")+"&shutdown="+c("chkShutdown")+
       "&pedales="+c("chkPedales"),{method:"POST"});
}

function cargarPantalla(datos){
 document.getElementById("chkBateria").checked=datos.chequeos.bateria;
 document.getElementById("chkShutdown").checked=datos.chequeos.shutdown;
 document.getElementById("chkPedales").checked=datos.chequeos.pedales;
 document.getElementById("bmsId").value="0x"+datos.bms.id.toString(16).toUpperCase();
 document.getElementById("bmsByte").value=datos.bms.byte;
 document.getElementById("bmsEscala").value=datos.bms.escala;
 document.getElementById("kbps").value=datos.kbps;
 document.getElementById("pruebaFreno").max=datos.pantalla.freno;
 document.getElementById("pruebaRpm").max=datos.pantalla.maximo;
 const canal=document.getElementById("rpmCanal");
 canal.innerHTML=datos.canales.map((c,i)=>'<option value="'+i+'">'+c.nombre+'</option>').join("");
 canal.value=datos.pantalla.canal;
 document.getElementById("rpmMaximo").value=datos.pantalla.maximo;
 document.getElementById("frenoMaximo").value=datos.pantalla.freno;
}

function guardarPantalla(){
 const v=id=>encodeURIComponent(document.getElementById(id).value);
 // Los deslizables de prueba usan las mismas escalas que la pantalla.
 document.getElementById("pruebaFreno").max=document.getElementById("frenoMaximo").value;
 document.getElementById("pruebaRpm").max=document.getElementById("rpmMaximo").value;
 fetch("/api/pantalla?canal="+v("rpmCanal")+"&maximo="+v("rpmMaximo")+
       "&freno="+v("frenoMaximo"),{method:"POST"});
}

// Prueba de barras. Al mover un deslizable se manda enseguida, pero sin
// apilar pedidos: si hay uno en viaje, se espera y se manda el ultimo
// valor. Ademas, mientras la casilla este marcada se reenvia cada medio
// segundo, porque la ECU suelta la prueba si deja de recibir.
let pruebaEnViaje=false, pruebaPendiente=false, pruebaLatido=null;

function valoresPrueba(){
 const v=id=>document.getElementById(id).value;
 document.getElementById("valorAcelerador").textContent=Math.round(v("pruebaAcelerador"));
 document.getElementById("valorFreno").textContent=Math.round(v("pruebaFreno"));
 document.getElementById("valorRpm").textContent=Math.round(v("pruebaRpm"));
 return "acelerador="+v("pruebaAcelerador")+"&freno="+v("pruebaFreno")+"&rpm="+v("pruebaRpm");
}

function mandarPrueba(){
 const parametros=valoresPrueba();
 if(!document.getElementById("probar").checked) return;
 if(pruebaEnViaje){pruebaPendiente=true;return;}
 pruebaEnViaje=true;
 fetch("/api/prueba?"+parametros,{method:"POST"}).finally(()=>{
  pruebaEnViaje=false;
  if(pruebaPendiente){pruebaPendiente=false;mandarPrueba();}
 });
}

function cambiarPrueba(){
 clearInterval(pruebaLatido);
 if(document.getElementById("probar").checked){
  mandarPrueba();
  pruebaLatido=setInterval(mandarPrueba,500);
 }else{
  fetch("/api/prueba?fin=1",{method:"POST"});
 }
}

function guardarNombre(id){
 const campo=document.querySelector('input[data-id="'+id+'"]');
 fetch("/api/nombre?id="+id+"&nombre="+encodeURIComponent(campo.value),
       {method:"POST"}).then(consultarNodos);
}

// Despues de mandar una orden se refresca enseguida, sin esperar al
// proximo refresco automatico, para que el boton se sienta inmediato.
function mandar(ruta){
 fetch(ruta,{method:"POST"}).then(consultar);
}

// La duracion viaja con la orden de arranque: asi la ECU la toma justo
// para esta ejecucion y no hay que "guardar" nada por separado.
function arrancar(){
 const segundos=document.getElementById("duracion").value;
 mandar("/api/rtd?duracion="+encodeURIComponent(segundos));
}

// Si la ECU deja de contestar, la pagina se atenua en vez de seguir
// mostrando numeros viejos como si fueran actuales.
function consultar(){
 fetch("/api/estado").then(respuesta=>respuesta.json()).then(dibujar)
  .catch(()=>document.body.classList.add("sinconexion"));
}
fetch("/api/estado").then(r=>r.json()).then(cargarPantalla).catch(()=>{});
consultar(); setInterval(consultar,500);
consultarNodos();
setInterval(consultarNodos,5000);
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
  server.on("/api/nodos", handleNodesRequest);
  server.on("/api/nombre", HTTP_POST, handleNameRequest);
  server.on("/api/pantalla", HTTP_POST, handleDisplayRequest);
  server.on("/api/prueba", HTTP_POST, handleTestRequest);
  server.on("/api/bms", HTTP_POST, handleBatteryRequest);
  server.on("/api/can", HTTP_POST, handleBitrateRequest);
  server.on("/api/calibrar", HTTP_POST, handleCalibrationRequest);
  server.on("/api/chequeos", HTTP_POST, handleChecksRequest);

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
  uint32_t durationMilliseconds = loggerGetDurationSeconds() * 1000UL;
  uint32_t progressPercent =
      loggerIsRecording() ? (elapsed * 100 / durationMilliseconds) : 100;
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
          "\",\"codigo\":" + String(errorGetCode()) +
          ",\"texto\":\"" + String(errorGetDescription(errorGetCode())) + "\"},";
  json += "\"tramas\":" + String(canGetReceivedFrameCount()) + ",";
  json += "\"recuperaciones\":" + String(canGetBusRecoveryCount()) + ",";
  json += "\"ruedas\":" + String(speedGetValidWheelCount()) + ",";
  json += "\"nodos\":" + String(sensorsCountPresentNodes()) + ",";
  json += "\"grabando\":" + String(loggerIsRecording() ? "true" : "false") + ",";
  json += "\"muestras\":" + String(loggerGetSampleCount()) + ",";
  json += "\"avance\":" + String(progressPercent) + ",";
  json += "\"duracion\":" + String(loggerGetDurationSeconds()) + ",";
  json += "\"control\":" +
          String(WEB_CONTROL_ENABLED_FOR_BENCH_TEST ? "true" : "false") + ",";
  /* Estado y configuracion del BMS. Los bytes crudos de la ultima trama
     van como texto para poder revisar el layout desde la pagina. */
  float batteryPercent = 0.0f;
  CanFrame batteryFrame;
  uint32_t batteryAge = 0;
  /* Pedales, shutdown y chequeos activos. */
  json += "\"pedales\":{\"sdc\":" + String(pedalsShutdownCircuitOpen() ? "true" : "false") +
          ",\"acelerador\":" + String(pedalsGetAcceleratorPercent(), 1) +
          ",\"diferencia\":" + String(pedalsGetAcceleratorDifferencePercent(), 1) +
          ",\"sensores\":[";
  for (uint8_t index = 0; index < PEDAL_SENSOR_COUNT; index++) {
    PedalSensorStatus status;
    pedalsGetSensorStatus(static_cast<PedalSensorIndex>(index), &status);
    if (index > 0) {
      json += ",";
    }
    json += "{\"nombre\":\"" + String(status.name) + "\",\"v\":" + String(status.volts, 2) +
            ",\"pct\":" + String(status.percent, 1) + ",\"reposo\":" + String(status.restVolts, 2) +
            ",\"fondo\":" + String(status.fullVolts, 2) +
            ",\"ok\":" + String(status.inRange ? "true" : "false") + "}";
  }
  StateChecks checks = stateGetChecks();
  json += "]},\"chequeos\":{\"bateria\":" + String(checks.batteryRequired ? "true" : "false") +
          ",\"shutdown\":" + String(checks.shutdownCircuit ? "true" : "false") +
          ",\"pedales\":" + String(checks.pedals ? "true" : "false") + "},";
  json += "\"kbps\":" + String(canGetBitrateKilobits()) + ",";
  json += "\"bms\":{\"id\":" + String(canGetBatteryIdentifier()) +
          ",\"byte\":" + String(canGetBatteryByte()) +
          ",\"escala\":" + String(canGetBatteryPercentPerCount(), 3) + ",\"bateria\":";
  json += canGetBatteryPercent(&batteryPercent) ? String(batteryPercent, 1) : String("null");
  json += ",\"bytes\":";
  if (canGetLastBatteryFrame(&batteryFrame, &batteryAge)) {
    char bytes[3 * 8 + 1] = "";
    for (uint8_t index = 0; index < batteryFrame.length && index < 8; index++) {
      snprintf(bytes + 3 * index, 4, index ? " %02X" : "%02X", batteryFrame.data[index]);
    }
    json += "\"" + String(bytes) + "\",\"edad\":" + String(batteryAge) + "},";
  } else {
    json += "null,\"edad\":0},";
  }
  json += "\"pantalla\":{\"canal\":" +
          String(static_cast<uint8_t>(displayGetRevolutionsChannel())) +
          ",\"maximo\":" + String(displayGetRevolutionsFullScale(), 0) +
          ",\"freno\":" + String(displayGetBrakeFullScale(), 0) + "},";
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
 * @brief Agrega a un JSON la descripcion de un nodo.
 *
 * @param[in,out]  json        Cadena a la que se agrega.
 * @param[in]      identifier  Identificador CAN del nodo.
 * @param[in]      known       true si figura en can_ids.h.
 * @param[in]      frameCount  Tramas recibidas (solo para desconocidos).
 *
 * @return void
 */
static void appendNodeJson(String &json, uint32_t identifier, bool known,
                           uint32_t frameCount) {
  /* Un nombre con comillas romperia el JSON: se las reemplaza. */
  char nameBuffer[NAME_MAXIMUM_LENGTH + 1];
  namesGet(identifier, nameBuffer, sizeof(nameBuffer));
  String name = nameBuffer;
  name.replace("\\", "/");
  name.replace("\"", "'");

  json += "{\"id\":" + String(identifier) + ",";
  json += "\"nombre\":\"" + name + "\",";
  json += "\"conocido\":" + String(known ? "true" : "false") + ",";
  json += "\"visto\":" + String(sensorsNodeWasEverSeen(identifier) ? "true" : "false") + ",";
  json += "\"callado\":" + String(sensorsNodeIsSilent(identifier) ? "true" : "false") + ",";
  json += "\"tramas\":" + String(frameCount) + "}";
}

/**
 * @brief Sirve la lista de nodos: los del mapa y los desconocidos.
 *
 * @return void
 */
static void handleNodesRequest(void) {
  String json = "[";

  for (uint8_t index = 0; index < sensorsGetKnownNodeCount(); index++) {
    if (index > 0) {
      json += ",";
    }
    appendNodeJson(json, sensorsGetKnownNodeIdentifier(index), true, 0);
  }

  for (uint8_t index = 0; index < sensorsGetUnknownIdentifierCount(); index++) {
    json += ",";
    appendNodeJson(json, sensorsGetUnknownIdentifier(index), false,
                   sensorsGetUnknownIdentifierFrameCount(index));
  }

  json += "]";
  server.send(200, "application/json", json);
}

/**
 * @brief Guarda el nombre de un nodo. Parametros: id (decimal) y nombre.
 *
 * @return void
 */
static void handleNameRequest(void) {
  if (!server.hasArg("id")) {
    server.send(400, "application/json", "{\"error\":\"falta id\"}");
    return;
  }

  uint32_t identifier = server.arg("id").toInt();
  namesSet(identifier, server.arg("nombre").c_str());

  server.send(200, "application/json", "{\"guardado\":true}");
}

/**
 * @brief Ajusta la pantalla. Parametros: canal, maximo y freno.
 *
 * Cada parametro es opcional; el que no viene, no se toca. El cambio se
 * ve en la pantalla en la vuelta siguiente.
 *
 * @return void
 */
static void handleDisplayRequest(void) {
  if (server.hasArg("canal") && server.hasArg("maximo")) {
    displaySetRevolutionsBar(static_cast<LogChannel>(server.arg("canal").toInt()),
                             server.arg("maximo").toFloat());
  }
  if (server.hasArg("freno")) {
    displaySetBrakeFullScale(server.arg("freno").toFloat());
  }
  server.send(200, "application/json", "{\"guardado\":true}");
}

/**
 * @brief Mueve las barras de la pantalla con valores de prueba.
 *
 * Parametros: acelerador (%), freno (bar) y rpm; o fin=1 para volver a
 * los datos reales. Los valores solo se muestran, no se graban.
 *
 * @return void
 */
static void handleTestRequest(void) {
  if (server.hasArg("fin")) {
    displayStopTest();
  } else {
    displaySetTestValues(server.arg("acelerador").toFloat(),
                         server.arg("freno").toFloat(),
                         server.arg("rpm").toFloat());
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

/**
 * @brief Elige la trama del BMS. Parametros: id (acepta 0x...), byte, escala.
 *
 * @return void
 */
static void handleBatteryRequest(void) {
  uint32_t identifier = strtoul(server.arg("id").c_str(), nullptr, 0);
  canSetBatteryLayout(identifier, server.arg("byte").toInt(),
                      server.arg("escala").toFloat());
  server.send(200, "application/json", "{\"guardado\":true}");
}

/**
 * @brief Guarda la velocidad del bus y reinicia la ECU para aplicarla.
 *
 * El controlador toma la velocidad al arrancar, por eso hace falta el
 * reinicio. Se responde primero para que la pagina no quede esperando.
 *
 * @return void
 */
static void handleBitrateRequest(void) {
  if (!canSetBitrateKilobits(server.arg("kbps").toInt())) {
    server.send(400, "application/json", "{\"error\":\"velocidad invalida\"}");
    return;
  }
  server.send(200, "application/json", "{\"reiniciando\":true}");
  delay(300);
  ESP.restart();
}

/**
 * @brief Calibra un pedal con la tension actual. Parametros: sensor
 *        (0 APPS 1, 1 APPS 2, 2 freno) y punto (reposo o fondo).
 *
 * @return void
 */
static void handleCalibrationRequest(void) {
  long sensor = server.arg("sensor").toInt();
  if (sensor < 0 || sensor >= PEDAL_SENSOR_COUNT) {
    server.send(400, "application/json", "{\"error\":\"sensor invalido\"}");
    return;
  }
  pedalsCalibrate(static_cast<PedalSensorIndex>(sensor), server.arg("punto") == "fondo");
  server.send(200, "application/json", "{\"guardado\":true}");
}

/**
 * @brief Activa o apaga los chequeos. Parametros: bateria, shutdown y
 *        pedales, cada uno 1 o 0.
 *
 * @return void
 */
static void handleChecksRequest(void) {
  StateChecks checks;
  checks.batteryRequired = server.arg("bateria") == "1";
  checks.shutdownCircuit = server.arg("shutdown") == "1";
  checks.pedals          = server.arg("pedales") == "1";
  stateSetChecks(checks);
  server.send(200, "application/json", "{\"guardado\":true}");
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

  /* La duracion se fija antes de encolar la orden, asi cuando la
     maquina de estados arranque la grabacion ya la encuentra puesta. Si
     no viene, queda la que habia. */
  if (server.hasArg("duracion")) {
    loggerSetDurationSeconds(server.arg("duracion").toInt());
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
 * @brief Corta la ejecucion antes de que se cumpla su duracion.
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
