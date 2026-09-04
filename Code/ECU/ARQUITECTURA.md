# Arquitectura del firmware

Vista de alto nivel de como esta armado el codigo de la ECU y de los
nodos sensores.

- Como correr la prueba: [PRUEBA.txt](../PRUEBA.txt)
- Decisiones tomadas y pendientes: [TODO.txt](TODO.txt)
- Mapa de identificadores CAN: [include/can_ids.h](include/can_ids.h)

---

## 1. Que hay conectado

Cada sensor vive en su propia placa y publica por CAN. La ECU no lee
ningun sensor de forma directa: solo escucha el bus.

```mermaid
flowchart LR
    N1["Nodo temperatura<br/>ESP32-C3 + DHT11<br/>ID 0x480 - 1 Hz"]
    N2["Nodo velocidad de giro<br/>ESP32-C3 + LM393<br/>ID 0x200 - 20 Hz"]
    BUS{{"Bus CAN 500 kbps"}}
    ECU["ECU<br/>ESP32-S3"]
    PC["Navegador<br/>celular o notebook"]

    N1 --> BUS
    N2 --> BUS
    BUS <--> ECU
    ECU -. "heartbeat 0x100 - 100 Hz" .-> BUS
    ECU ---|"WiFi propio<br/>192.168.4.1"| PC
```

Cada placa necesita su transceiver: el controlador del ESP32 habla en
niveles logicos de 3.3 V, y el bus CAN es diferencial.

---

## 2. Maquina de estados del vehiculo

Un solo lugar del programa decide en que estado esta el auto:
[state.cpp](src/state.cpp).

```mermaid
stateDiagram-v2
    [*] --> BOOT

    BOOT --> CONFIG : nodos de seguridad OK, tras 2 s de escucha
    BOOT --> FAULT : falla de nodo de seguridad
    CONFIG --> CAR_READY : nodos presentes configurados
    CAR_READY --> CAR_ON : secuencia RTD completa
    CAR_ON --> CAR_READY : 90 s cumplidos, o apagado pedido

    CONFIG --> FAULT : error CRITICAL
    CAR_READY --> FAULT : error CRITICAL
    CAR_ON --> FAULT : error CRITICAL

    note right of FAULT
        Sin salida por software.
        Solo se sale reiniciando el micro.
    end note
```

El led RGB de debug muestra el estado: BOOT azul, CONFIG amarillo,
CAR_READY verde fijo, CAR_ON verde pulsante, FAULT rojo.

CONFIG dura unos 10 ms mientras no exista el mensaje de configuracion
de nodos, asi que en el led practicamente no se ve.

---

## 3. Como esta dividido el codigo de la ECU

Tres tasks. El nucleo 1 corre lo que tiene que ser predecible, y el 0
queda para la radio, que es donde el stack de WiFi ya corre lo suyo.

Las tasks no comparten variables sueltas: se hablan por colas.

```mermaid
flowchart LR
    subgraph nucleo1["Nucleo 1 - tiempo real"]
        direction TB
        taskCan["taskCan - prioridad 4<br/>can.cpp"]
        taskState["taskState - prioridad 3<br/>state.cpp"]
    end

    subgraph nucleo0["Nucleo 0 - radio"]
        taskWifi["taskWifi - prioridad 1<br/>wifi.cpp"]
    end

    taskState -- "queueWifi<br/>encender / apagar" --> taskWifi
    taskWifi -- "queueEvents<br/>arrancar / detener" --> taskState
```

Los demas archivos no tienen task propia: son modulos que las tasks
llaman.

| Archivo | Que hace | Quien lo usa |
| --- | --- | --- |
| `state.cpp` | Maquina de estados | taskState |
| `can.cpp` | Bus CAN: envio, recepcion y despacho | taskCan |
| `wifi.cpp` | Punto de acceso y actualizacion OTA | taskWifi |
| `sensors.cpp` | Que nodos estan presentes | taskCan, taskState |
| `logger.cpp` | Guarda las muestras de la ejecucion | taskCan, webmonitor |
| `errors.cpp` | Registra y latchea las fallas | las tres tasks |
| `indicators.cpp` | Led RGB de debug | taskState |
| `webmonitor.cpp` | Pagina, JSON y CSV | taskWifi |

El bus CAN tiene mas prioridad que la maquina de estados a proposito:
no puede perder tramas esperando a que los estados terminen su vuelta.

---

## 4. Que pasa durante una ejecucion

```mermaid
sequenceDiagram
    participant Nodo as Nodo C3
    participant Can as taskCan
    participant Log as logger
    participant Est as taskState
    participant Web as webmonitor
    participant Nav as Navegador

    Note over Nodo,Can: El nodo emite solo, sin que nadie le pregunte

    Nodo->>Can: trama CAN
    Can->>Can: marca el nodo como presente
    Can->>Log: valor convertido a unidad real
    Note over Log: fuera de la ejecucion solo<br/>actualiza el valor en vivo

    Nav->>Web: POST /api/rtd
    Web->>Est: evento por queueEvents
    Est->>Log: loggerStart

    loop 90 segundos
        Nodo->>Can: trama CAN
        Can->>Log: se guarda la muestra
    end

    Est->>Log: la ejecucion termina sola
    Nav->>Web: GET /registro.csv
    Web->>Log: lee las muestras
    Web-->>Nav: CSV con una columna por canal
```

El nodo nunca espera una orden: arranca midiendo y emitiendo. Eso es lo
que permite que la ECU lo descubra con solo escuchar, y que un nodo
ausente no trabe nada.

---

## 5. Como se agrega un sensor nuevo

Del lado del nodo, un sensor son cuatro funciones declaradas en
[SensorNode.h](../SensorNode/include/SensorNode.h). `main.cpp` no sabe que
sensor tiene conectado: se lo pregunta a esa interfaz.

```mermaid
flowchart LR
    main["main.cpp<br/>no sabe que sensor hay"]
    api["Interfaz del sensor"]
    s1["sensor_temperature.cpp"]
    s2["sensor_wheel_speed.cpp"]
    s3["sensor_nuevo.cpp"]

    main --> api
    api --> s1
    api --> s2
    api -.-> s3

    s1 -.-> nota["sensorInitialize<br/>sensorGetCanIdentifier<br/>sensorGetPeriodMilliseconds<br/>sensorBuildFrame"]
```

Los pasos:

1. Copiar uno de los `sensor_*.cpp` e implementar las cuatro funciones.
2. Definir el identificador y el layout de su trama en `can_ids.h`:
   unidad, escala, rango y endianness. Si emisor y receptor no coinciden
   en los cuatro, el dato no falla, miente en silencio.
3. Agregar un entorno en el `platformio.ini` del nodo. El archivo del
   sensor se elige con `build_src_filter`, asi que no hace falta ningun
   `#ifdef`.
4. Del lado de la ECU, sumar el handler en `can.cpp` y el canal en
   `LogChannel`.

No hay que tocar `main.cpp` ni `can.cpp` del nodo.
