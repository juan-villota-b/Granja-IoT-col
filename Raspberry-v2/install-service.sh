#!/bin/bash
# install-service.sh — Instala servicio systemd para ip6tables persistente
# Ejecutar UNA VEZ en la RPi: sudo ./install-service.sh
# La regla se aplica automáticamente al bootear la Raspberry

set -e

SERVICE_NAME="otbr-coap-ip6tables"
SERVICE_FILE="/etc/systemd/system/${SERVICE_NAME}.service"
SCRIPT_FILE="/usr/local/bin/otbr-wait-and-iptables.sh"

echo "Instalando servicio systemd: ${SERVICE_NAME}..."

# Crear script helper
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

# Crear servicio systemd
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

echo ""
echo "Servicio instalado y habilitado."
echo "La regla se aplica automáticamente al iniciar la Raspberry."
echo ""
echo "Para aplicar ahora sin reiniciar:"
echo "  sudo systemctl start ${SERVICE_NAME}.service"
echo ""
echo "Para verificar:"
echo "  sudo systemctl status ${SERVICE_NAME}.service"
echo "  docker exec otbr ip6tables -L INPUT -n | grep 5685"
