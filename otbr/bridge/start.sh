#!/bin/bash
set -e

echo "================================================"
echo " IoT Gateway Bridge - Starting"
echo "================================================"

if [ "$START_SIMULATION" = "true" ]; then
    echo "Starting simulation nodes..."
    cd /app
    python -m simulation.network_sim --base-port 15683 &
    SIM_PID=$!
    echo "Simulation PID: $SIM_PID"
    sleep 3
fi

echo "Starting bridge..."
exec python /app/main.py -c /app/config.yaml
