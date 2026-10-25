#!/usr/bin/env bash
# dev.sh — run the dashboard locally without Docker.
# Usage: ./scripts/dev.sh

set -euo pipefail
cd "$(dirname "$0")/.."

exec uvicorn app.main:app --host 0.0.0.0 --port 3000
