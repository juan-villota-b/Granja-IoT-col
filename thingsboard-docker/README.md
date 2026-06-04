# ThingsBoard CE — Servidor Central IoT

ThingsBoard **Community Edition** con PostgreSQL. Es el servidor central que recibe y sincroniza datos desde los Edge locales desplegados en campo (Raspberry Pi, sistemas embebidos).

## Rol en la arquitectura

```
┌────────────────────────────────────────────┐
│            THINGSBOARD CE (:8080)          │
│                                            │
│  Consola central: dashboards, reglas,      │
│  dispositivos, telemetría histórica.       │
│  Sincroniza con N Edges vía RPC :7070.     │
│                                            │
│  ┌──────────┐   ┌──────────────────────┐  │
│  │ Postgres │   │  RPC Server :7070    │  │
│  └──────────┘   └──────────────────────┘  │
└──────────────────┬─────────────────────────┘
                   │ CLOUD_RPC host:7070
    ┌──────────────┼──────────────┐
    │              │              │
  Edge-1        Edge-2        Edge-N
  (finca)       (...)
```

| Qué llega | Cómo llega |
|-----------|-----------|
| Telemetría de sensores | Edge → Cloud RPC (MQTT) |
| Dispositivos registrados | Edge sincroniza automáticamente |
| Comandos RPC | Dashboard → Cloud → Edge → Bridge → Nodo |
| Reglas y alarmas | Se configuran en CE y se replican a Edges |

## Servicio

| Contenedor | Imagen | Puerto | Rol |
|-----------|--------|--------|-----|
| `thingsboard` | `thingsboard/tb-postgres` | `:8080` (web) | Servidor ThingsBoard CE |
| | | `:1883` (MQTT) | Ingesta directa |
| | | `:5683/udp` (CoAP) | Ingesta directa |
| | | `:7070` (RPC) | Sincronización con Edges |

## Despliegue

```bash
cd thingsboard-docker
docker compose up -d
```

## Acceso

| URL | Descripción |
|-----|-------------|
| `http://localhost:8080` | Consola ThingsBoard |
| `http://localhost:8080/api/auth/login` | Login API |

## Comunicación con los Edges

Los Edge locales (RPi, sistemas embebidos) se conectan al CE mediante **Cloud RPC**:

```
Edge → CLOUD_RPC_HOST:{CE_IP}:7070 → ThingsBoard CE
```

Para que un Edge se conecte, necesita:

1. **CLOUD_ROUTING_KEY** y **CLOUD_ROUTING_SECRET** — se generan desde:
   - CE: **Edge Management → Edges → +** → crear Edge
   - Copiar las credenciales generadas

2. **CLOUD_RPC_HOST** — IP del host donde corre este CE

3. **Puerto 7070** accesible desde el Edge (firewall, Docker network)

## Persistencia

```yaml
volumes:
  tb-data:      # Base de datos PostgreSQL (telemetría + dispositivos)
  tb-logs:      # Logs del servidor
```

## Diferencia con TB Edge

| Característica | ThingsBoard CE (:8080) | ThingsBoard Edge (:8082) |
|---------------|----------------------|-------------------------|
| Ubicación | Servidor central (nube/local) | Dispositivo de campo (RPi) |
| Base de datos | PostgreSQL (este contenedor) | PostgreSQL (Edge local) |
| Sincronización | Recibe de Edges | Envía a CE |
| Almacenamiento | Ilimitado (depende del servidor) | Limitado (SD card) |
| Conexión a internet | Requerida para acceder desde fuera | No requiere (funciona offline) |
| Roles de usuario | Tenant, Customer, etc. | Solo Tenant |
| Gestión de Edges | Sí — panel Edge Management | No |

## Cosas a tener en cuenta

- El primer inicio tarda **1-2 minutos** en inicializar PostgreSQL y las tablas
- Los datos persisten en volúmenes Docker. Para resetear: `docker compose down -v`
- Para conectar un Edge, crear Edge en CE y copiar las credenciales al docker-compose del Edge
- La telemetría de los sensores llega vía Edge, no directamente (aunque soporta ingesta directa por MQTT)
