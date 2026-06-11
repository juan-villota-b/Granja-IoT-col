#pragma once

/* ── Habilitar Observe para notificaciones push ── */
#define OPENTHREAD_CONFIG_COAP_OBSERVE_API_ENABLE  1

/* ── Poll period del SED: cada 5s pregunta al padre si hay datos ──
   La radio duerme entre polls. Valores tipicos: 1s-30s.
   Bateria: 1s → semanas, 5s → meses, 30s → >1 año             */
#define OPENTHREAD_CONFIG_MAC_DEFAULT_DATA_POLL_PERIOD  5000

/* ── Limpiar datos de red antiguos al iniciar ── */
#define OPENTHREAD_CONFIG_STORE_FRAME_COUNTER_AHEAD  0
