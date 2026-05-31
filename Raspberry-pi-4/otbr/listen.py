#!/usr/bin/env python3
import socket
import struct
import json
PUERTO = 5689
GRUPO_MULTICAST = "ff03::1"
sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
# Unirse al grupo multicast realm-local (todos los nodos Thread)
for iface_name in ["wpan0", "eth0", "wlan0"]:
    try:
        ifidx = socket.if_nametoindex(iface_name)
        sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_JOIN_GROUP,
                        struct.pack("16sI", socket.inet_pton(socket.AF_INET6, GRUPO_MULTICAST), ifidx))
        print(f"  unido a {GRUPO_MULTICAST} en {iface_name}")
    except (OSError, AttributeError):
        pass
sock.bind(("", PUERTO))
print(f"Escuchando en puerto {PUERTO} ...")
while True:
    datos, addr = sock.recvfrom(1024)
    try:
        msg = json.loads(datos.decode())
        print(f"[{addr[0]}] seq={msg['seq']}  temp={msg['temp']}°C  hum={msg['hum']}%")
    except Exception as e:
        print(f"[{addr[0]}] raw: {datos}  error: {e}")
