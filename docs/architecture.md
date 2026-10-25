# Arquitectura

Documento de referencia técnica: cómo se comunican los nodos, el gateway y el servidor, qué formato tiene cada mensaje y por qué se tomaron las decisiones de diseño.

- [Topología de red](#topología-de-red)
- [Protocolos por capa](#protocolos-por-capa)
- [Aprovisionamiento](#aprovisionamiento)
- [Telemetría: CoAP + CBOR](#telemetría-coap--cbor)
- [Downlink: comandos piggyback](#downlink-comandos-piggyback)
- [MQTT y ThingsBoard](#mqtt-y-thingsboard)
- [Bajo consumo: modo SED](#bajo-consumo-modo-sed)
- [Control automático de riego](#control-automático-de-riego)
- [Limitaciones conocidas](#limitaciones-conocidas)

---

## Topología de red

```
Thread mesh (802.15.4)                    IPv6 / infraestructura
┌───────────────────────────┐            ┌──────────────────────────────┐
│ light-node                │            │ Raspberry Pi 4               │
│ temperature-node          │◄──6LoWPAN──►│ wpan0 (OTBR)                 │
│ soil-node                 │            │ bridge :5685/udp             │
│ valve-node                │            │ TB Edge :1883 (MQTT interno) │
└───────────────────────────┘            └──────────────┬───────────────┘
                                                        │ Cloud RPC :7070
                                                        ▼
                                         ┌──────────────────────────────┐
                                         │ ThingsBoard CE :8080         │
                                         │ dashboard :3000              │
                                         └──────────────────────────────┘
```

El gateway ejecuta el **OpenThread Border Router** con un nRF52840 como RCP. Los nodos se unen al *dataset* Thread grabado en el firmware y obtienen una dirección IPv6 global. El bridge escucha en una dirección *anycast* del border router (`fd29:c51e:a87a:e5e5:0:ff:fe00:fc00`), por lo que la dirección del gateway es estable aunque cambie la topología.

---

## Protocolos por capa

| Capa | Protocolo | Implementación |
|------|-----------|----------------|
| Enlace | IEEE 802.15.4 | Radio nativa del ESP32-C6 |
| Red | 6LoWPAN + IPv6 | OpenThread (MTD/SED en los nodos, FTD en el OTBR) |
| Transporte | CoAP (UDP) | libcoap en el nodo (`esp-coap`), aiocoap en el bridge |
| Serialización | CBOR | Codificación manual en el nodo, `cbor2` en el bridge |
| Gateway | MQTT 3.1.1 | paho-mqtt contra ThingsBoard Edge |
| Servidor | REST + Cloud RPC | ThingsBoard CE/Edge |
| Presentación | HTTP + WebSocket | FastAPI + SPA vanilla |

La elección de CoAP + CBOR responde al costo por mensaje: un reporte típico ocupa entre 25 y 35 bytes frente a más de 150 de un POST HTTP con JSON.

---

## Aprovisionamiento

El *access token* de ThingsBoard de cada dispositivo se compila en el firmware como `PROV_KEY` (`main/node_config.h`). Al arrancar, el nodo envía un POST CoAP especial:

```cbor
{ "prov_key": "TmueBdWJgGwWdRMGAABv", "state": "pending" }
```

El bridge responde según el estado del dispositivo en ThingsBoard:

| Situación | Respuesta CBOR | Efecto en el nodo |
|-----------|----------------|-------------------|
| `prov_key` desconocido | `{"cmd": "wait", "msg": "Device not provisioned yet"}` | Reintenta más tarde |
| Dispositivo nuevo, `state = pending` | `{"cmd": "start", "name": "<NODO_ID>"}` | Confirma el nombre y empieza a reportar |
| Dispositivo ya registrado | `{"cmd": "start"}` | Empieza a reportar |

El bridge resuelve `prov_key` consultando el atributo `provisioning_key` (`SERVER_SCOPE`) de todos los dispositivos del tenant en TB Edge y cachea la relación en `/data/registered_nodes.json`, de modo que el arranque siguiente no requiere consultar la API.

---

## Telemetría: CoAP + CBOR

**Endpoint:** `POST coap://[<bridge>]:5685/readings`, siempre como mensaje **CON** (confirmable).

### Payload de registro (primer envío)

```cbor
{
  "id": "Nodo-Temperatura",   // NODE_ID
  "tp": "sensor_sed",         // tipo de dispositivo
  "v":  "5.2.0",              // versión de firmware
  "t":  24.7,                 // variable del sensor
  "lat": 5.029153,            // posición
  "lng": -75.472661,
  "r":  -62,                  // RSSI del padre Thread
  "u":  42                    // uptime en segundos
}
```

### Payload posterior

```cbor
{ "id": "Nodo-Temperatura", "t": 24.7, "r": -62, "u": 72 }
```

`soil-node` sigue una variante: identifica el dispositivo con `prov_key` en todos sus envíos (`{prov_key, h, r, u}`) en lugar de `id`, y el bridge lo resuelve contra el atributo `provisioning_key` de ThingsBoard.

### Claves de telemetría

| Clave | Origen | Tipo | Unidad |
|-------|--------|------|--------|
| `t` | DHT22 | float16 | °C |
| `h` | HW-390 | uint8 | % de humedad de suelo |
| `l` | LDR | uint8 | % de luz |
| `v` | Relé | uint8 | 0 = cerrada, 1 = abierta |
| `b` | ADC | uint16 | mV de batería |
| `r` | OpenThread | int8 | dBm |
| `u` | `xTaskGetTickCount` | uint32 | s |

El bridge traduce las claves cortas a nombres de ThingsBoard: `t → temperature`, `h → humidity`, `l → light`, `v → valve`, `b → battery`, `r → rssi`, `u → uptime`.

### Reintentos

Cada envío crea su propio `coap_context_t` y `coap_session_t`, y espera el ACK 2.xx con un timeout de 1500 ms:

| Envío | Reintentos | Tiempo máximo |
|-------|-----------|---------------|
| Registro | 3 | 4.5 s |
| Telemetría | 2 | 3.0 s |

El primer intento de cada envío suele expirar porque la ruta UDP sobre 6LoWPAN aún no está resuelta; el segundo aprovecha la caché de vecinos y responde en decenas de milisegundos. Por eso el bridge puede registrar el mismo timestamp dos veces: ThingsBoard deduplica por `ts`, no por payload.

### Disparo del envío

```c
debe_enviar = (fabsf(actual - anterior) >= umbral)     // cambio significativo
           || (uptime - ultimo_envio >= heartbeat_s);  // keepalive
```

| Nodo | Umbral por defecto | Heartbeat | Periodo de muestreo |
|------|--------------------|-----------|---------------------|
| light-node | 5 % | 300 s | 30 s |
| temperature-node | 0.1 °C (calibrado en NVS) | 300 s | 30 s |
| soil-node | 5 % | 300 s | 30 s |
| valve-node | — | 1 s | 1 s |

---

## Downlink: comandos piggyback

El bridge mantiene `pending_commands[nid]`. Cuando llega un POST de ese nodo, adjunta el comando en la respuesta CoAP (2.05 Content) y lo elimina de la cola:

```
Dashboard ──REST──► TB CE ──RPC set_valve──► TB Edge ──MQTT──► Bridge
                                                                 │
                                                    pending_commands["BOMBA"] = {"v": 1}
                                                                 │
ESP32 ◄── 2.05 Content {"v": 1} ────────────────────────────────┘
```

Métodos RPC soportados por el bridge:

| Método | Parámetros | Efecto |
|--------|------------|--------|
| `set_valve` | `{"state": 0\|1}` | Encola `{"v": state}` |
| `set_config` / `set_thresholds` | `{"tt": float, "hb": int}` | Encola `{"tt", "hb"}` |
| `start_publishing` | — | Encola `{"cmd": "start"}` |

Solo `valve-node` interpreta el payload de respuesta: espera un mapa CBOR de una entrada `{"v": 0|1}` y acciona el relé. Los nodos sensores verifican el ACK 2.xx pero descartan el cuerpo, de modo que comandos como `set_thresholds` aún no se aplican en ellos. Ver [Limitaciones conocidas](#limitaciones-conocidas).

---

## MQTT y ThingsBoard

El bridge soporta dos modos, seleccionables con `mqtt_mode` en `gateway/bridge/config.yaml`:

| Modo | Sesión MQTT | Tópicos |
|------|-------------|---------|
| `direct` (activo) | Una conexión por dispositivo con su propio *access token* | `v1/devices/me/telemetry`, `v1/devices/me/attributes` |
| `gateway` | Una conexión de gateway con el token de `IoT-Gateway` | `v1/gateway/telemetry`, `v1/gateway/connect`, `v1/gateway/attributes`, `v1/gateway/rpc` |

En modo `direct` los tokens viven en `device_tokens` y el gateway no necesita la marca *Is gateway* en ThingsBoard. El modo `gateway` permite registrar dispositivos nuevos sin token propio, a cambio de una sesión compartida.

**Sincronización Edge → Cloud:** ThingsBoard Edge se conecta al servidor central mediante *Cloud RPC* (`CLOUD_RPC_HOST:7070`). Los dispositivos, atributos y telemetría se replican automáticamente; el dashboard consulta siempre el CE.

---

## Bajo consumo: modo SED

Los nodos son **Sleepy End Device**: no mantienen el radio escuchando. Tres mecanismos combinados:

| Mecanismo | Configuración | Efecto |
|-----------|---------------|--------|
| Link mode | `mRxOnWhenIdle = 0`, `mDeviceType = 0` | El radio duerme entre sondeos |
| Poll period | 30 s | Un sondeo al padre por ciclo |
| Tickless idle | `CONFIG_FREERTOS_USE_TICKLESS_IDLE=y` + `CONFIG_PM_ENABLE=y` | La CPU entra en *light sleep* durante `vTaskDelay` |

El modo se fuerza **antes** de habilitar Thread y se vuelve a verificar tras el *join*, porque OpenThread persiste `rx-on-when-idle` en NVS. Si ese flag queda en 1, el radio nunca duerme y el consumo pasa de ~0.7 mA a ~25 mA.

```
t=0      Leer sensor (1 ms)
t=0.001  ¿Δ supera umbral o heartbeat?  ── no ──► vTaskDelay(30 s) ──► light sleep
t=0.002  ── sí ──► push CoAP (≈50 ms con ruta caliente)
t=0.052  vTaskDelay(30 s) ──► light sleep
```

El consumo medido con el nodo en reposo es del orden de **0.7 mA promedio sin USB**. Con el cable USB conectado, el PHY del *Serial/JTAG* añade ~10 mA que no dependen del firmware.

---

## Control automático de riego

`gateway/bridge/irrigation/controller.py` recibe cada telemetría y mantiene el último valor por nodo. En cada mensaje evalúa:

```
si humedad_suelo >= soil_high_pct        → CERRAR
si no:
    humedad_suelo <  soil_low_pct
    y luz        >= light_min_pct
    y temp       <  temp_max_c
    y hora entre allowed_start_hour y allowed_end_hour
    y (ahora - ultimo_comando) >= min_cycle_seconds
                                          → ABRIR
```

Umbrales por defecto (`config.yaml`):

| Clave | Valor | Significado |
|-------|-------|-------------|
| `soil_low_pct` | 30 | Humedad bajo la cual se riega |
| `soil_high_pct` | 70 | Humedad que cierra la válvula |
| `light_min_pct` | 10 | Evita regar de noche |
| `temp_max_c` | 35.0 | Evita riego en calor extremo |
| `min_cycle_seconds` | 60 | Anti-ciclado |
| `allowed_start_hour` / `allowed_end_hour` | 6 / 23 | Ventana horaria |
| `actuator_nid` | `BOMBA` | Nodo que recibe el comando |

El controlador envía el RPC a TB Edge **directamente en la red local** (`tb_edge.url`), sin pasar por la nube: el riego sigue funcionando si el enlace a internet cae.

---

## Limitaciones conocidas

| Tema | Detalle |
|------|---------|
| Downlink parcial | `valve-node` aplica `{"v": 0\|1}`; los nodos sensores solo verifican el ACK 2.xx y descartan el cuerpo, por lo que `set_thresholds` y `set_config` quedan sin efecto. |
| Colisión de clave `v` | `light-node` y `temperature-node` usan `v` para la versión de firmware en el primer envío, mientras el bridge interpreta `v` como estado de válvula. El primer POST de esos nodos provoca un error de conversión: el registro se completa, pero esa muestra no se publica. `soil-node` envía `prov_key` en su lugar y no se ve afectado. |
| Secretos en el repositorio | `config.yaml` contiene tokens MQTT de dispositivo y credenciales de TB Edge en texto plano; `node_config.h` incluye el `PROV_KEY`. Aceptable en laboratorio, no en producción. |
| Certificados | ThingsBoard Edge usa HTTP/MQTT sin TLS dentro de la red local. |
