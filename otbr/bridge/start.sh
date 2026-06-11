#!/bin/bash
set -e

echo "================================================"
echo " IoT Gateway Bridge - Starting"
echo "================================================"

echo "Starting bridge..."
exec python /app/main.py -c /app/config.yaml
