#!/bin/bash
set -e

echo "================================================"
echo " IoT Gateway Bridge - Starting"
echo "================================================"

mkdir -p /data

echo "Starting bridge..."
exec python /app/main.py -c /app/config.yaml
