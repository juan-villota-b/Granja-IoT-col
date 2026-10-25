#!/usr/bin/env bash
# compose-up.sh — build and start the dashboard stack.
# Usage: ./scripts/compose-up.sh              # all services
#        ./scripts/compose-up.sh dashboard     # a single service
#
# The image is always rebuilt: a cached layer can serve stale code.

set -euo pipefail
cd "$(dirname "$0")/.."

docker compose up -d --build "$@"
