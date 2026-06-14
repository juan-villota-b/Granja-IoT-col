#!/bin/bash
# start.sh — levanta todos los servicios con rebuild forzado
# Uso: ./start.sh              (levanta todo)
#      ./start.sh granja-dashboard  (solo un servicio)

set -e
cd "$(dirname "$0")"
docker compose up -d --build "$@"
