#!/bin/bash
# start.sh — levanta todos los servicios con rebuild forzado
# Uso: ./start.sh              (levanta todo)
#      ./start.sh granja-dashboard  (solo un servicio)

set -e
cd "$(dirname "$0")"
docker compose up -d --build "$@"

# Abrir puerto CoAP en ip6tables para que el bridge reciba datos de Thread
# NOTA: con network_mode=host, el OTBR comparte la red del host.
# Los paquetes Thread llegan via wpan0 y son entregados al Bridge
# porque escucha en [::]:5685 (todas las interfaces).
docker exec otbr ip6tables -I INPUT 1 -p udp --dport 5685 -j ACCEPT 2>/dev/null || true
