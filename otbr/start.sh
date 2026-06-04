#!/bin/bash
# start.sh — levanta todos los servicios con rebuild forzado
# Uso: ./start.sh              (levanta todo)
#      ./start.sh granja-dashboard  (solo un servicio)

set -e
cd "$(dirname "$0")"
docker compose up -d --build "$@"

# Abrir puerto CoAP en ip6tables para que el bridge reciba datos de Thread
docker exec otbr ip6tables -I INPUT 1 -p udp --dport 5685 -j ACCEPT 2>/dev/null || true
