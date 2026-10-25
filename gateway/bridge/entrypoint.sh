#!/usr/bin/env bash
# entrypoint.sh — container entrypoint for the CoAP/MQTT bridge.
set -euo pipefail

echo "================================================"
echo " IoT Gateway Bridge - starting"
echo "================================================"

mkdir -p /data

exec python /app/main.py -c /app/config.yaml
