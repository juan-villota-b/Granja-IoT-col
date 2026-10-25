#!/usr/bin/env bash
# install-otbr-iptables-service.sh — persist the CoAP ip6tables rule across reboots.
#
# Run once on the Raspberry Pi: sudo ./scripts/install-otbr-iptables-service.sh
# The rule is re-applied automatically at boot, once the OTBR container is up.

set -euo pipefail

SERVICE_NAME="otbr-coap-ip6tables"
SERVICE_FILE="/etc/systemd/system/${SERVICE_NAME}.service"
SCRIPT_FILE="/usr/local/bin/otbr-wait-and-iptables.sh"

echo "Installing systemd service: ${SERVICE_NAME}..."

sudo tee "$SCRIPT_FILE" > /dev/null << 'HELPER'
#!/bin/bash
i=0
while [ $i -lt 30 ]; do
    docker ps -q -f name=otbr 2>/dev/null | grep -q . && break
    sleep 2
    i=$((i + 1))
done
docker exec otbr ip6tables -I INPUT 1 -p udp --dport 5685 -j ACCEPT 2>/dev/null || true
HELPER

sudo chmod +x "$SCRIPT_FILE"

sudo tee "$SERVICE_FILE" > /dev/null << 'UNIT'
[Unit]
Description=OTBR CoAP ip6tables rule for port 5685
After=docker.service network-online.target
Wants=network-online.target

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/usr/local/bin/otbr-wait-and-iptables.sh
ExecStop=/bin/sh -c 'docker exec otbr ip6tables -D INPUT -p udp --dport 5685 -j ACCEPT 2>/dev/null || true'

[Install]
WantedBy=multi-user.target
UNIT

sudo systemctl daemon-reload
sudo systemctl enable "${SERVICE_NAME}.service"

cat << EOF

Service installed and enabled: the rule is applied at boot.

Apply it now without rebooting:
  sudo systemctl start ${SERVICE_NAME}.service

Verify:
  sudo systemctl status ${SERVICE_NAME}.service
  docker exec otbr ip6tables -L INPUT -n | grep 5685
EOF
