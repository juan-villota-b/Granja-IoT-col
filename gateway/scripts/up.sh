#!/usr/bin/env bash
# up.sh — build and start every field-gateway service on the Raspberry Pi.
# Usage: ./scripts/up.sh [service]

set -euo pipefail
cd "$(dirname "$0")/.."

echo "================================================"
echo " Granja IoT - Field Gateway"
echo "================================================"

# 1. Bring the stack up with a forced rebuild.
docker compose up -d --build "$@"

# 2. ip6tables rule: allow inbound CoAP from the Thread mesh.
#    Thread packets arrive on wpan0 while the bridge listens on [::]:5685.
echo "Applying ip6tables rule for CoAP :5685..."
docker exec otbr ip6tables -I INPUT 1 -p udp --dport 5685 -j ACCEPT 2>/dev/null || true

echo ""
echo "Services:"
echo "  OTBR Web     -> http://localhost:8083"
echo "  OTBR REST    -> http://localhost:8081"
echo "  TB Edge Web  -> http://localhost:8082"
echo "  CoAP Bridge  -> [::]:5685"
echo ""
echo "Logs:"
echo "  docker logs -f iot-bridge"
echo "  docker logs -f otbr"
echo "  docker logs -f gateway-mytbedge-1"
