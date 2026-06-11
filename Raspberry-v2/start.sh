#!/bin/bash
# start.sh — levanta todos los servicios en la Raspberry Pi
# Uso: ./start.sh

set -e
cd "$(dirname "$0")"

echo "================================================"
echo " Granja IoT - Raspberry-v2 Gateway"
echo "================================================"

# 1. Levantar servicios con rebuild forzado
docker compose up -d --build "$@"

# 2. Regla ip6tables: permitir tráfico CoAP entrante desde Thread
#    Los paquetes Thread llegan via wpan0 al Bridge que escucha en [::]:5685
echo "Aplicando regla ip6tables para CoAP :5685..."
docker exec otbr ip6tables -I INPUT 1 -p udp --dport 5685 -j ACCEPT 2>/dev/null || true

echo ""
echo "Servicios:"
echo "  OTBR Web     → http://localhost:8083"
echo "  OTBR REST    → http://localhost:8081"
echo "  TB Edge Web  → http://localhost:8082"
echo "  CoAP Bridge  → [::]:5685"
echo ""
echo "Para ver logs:"
echo "  docker logs -f iot-bridge"
echo "  docker logs -f otbr"
echo "  docker logs -f raspberry-v2-mytbedge-1"
