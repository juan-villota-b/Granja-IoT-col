#!/usr/bin/env python3
"""Configura el customer "Finca" y el usuario "juan" en TB CE.

Uso: python3 setup_customer.py
"""
import httpx
import asyncio
import sys

TB_URL = "http://localhost:8080"
EDGE_NAME = "Granja-Raspberry"
CUSTOMER_TITLE = "Finca"
USER_EMAIL = "juan@finca.com"
USER_PASSWORD = "juan123"


async def main():
    async with httpx.AsyncClient(timeout=30.0) as client:
        # 1. Login as tenant
        print("[1/7] Autenticando como tenant...")
        r = await client.post(
            f"{TB_URL}/api/auth/login",
            json={"username": "tenant@thingsboard.org", "password": "tenant"},
        )
        if r.status_code != 200:
            print(f"ERROR: Login fallo ({r.status_code})")
            sys.exit(1)
        token = r.json()["token"]
        headers = {"X-Authorization": f"Bearer {token}", "Content-Type": "application/json"}

        # 2. Crear Customer
        print("[2/7] Creando Customer 'Finca'...")
        r = await client.post(
            f"{TB_URL}/api/customer",
            json={"title": CUSTOMER_TITLE},
            headers=headers,
        )
        if r.status_code == 200:
            customer = r.json()
            customer_id = customer.get("id", {}).get("id")
            print(f"  Customer creado: {customer_id}")
        else:
            # Buscar customer existente
            r2 = await client.get(
                f"{TB_URL}/api/customers?pageSize=100&page=0&textSearch={CUSTOMER_TITLE}",
                headers=headers,
            )
            customers = r2.json().get("data", [])
            found = [c for c in customers if c.get("title") == CUSTOMER_TITLE]
            if found:
                customer = found[0]
                customer_id = customer.get("id", {}).get("id")
                print(f"  Customer ya existe: {customer_id}")
            else:
                print(f"  ERROR al crear customer: {r.status_code} {r.text}")
                sys.exit(1)

        # 3. Crear Customer User
        print("[3/7] Creando usuario 'juan'...")
        r = await client.post(
            f"{TB_URL}/api/user?sendActivationMail=false",
            json={
                "email": USER_EMAIL,
                "authority": "CUSTOMER_USER",
                "firstName": "Juan David",
                "lastName": "Finca",
                "customerId": {"id": customer_id, "entityType": "CUSTOMER"},
            },
            headers=headers,
        )
        if r.status_code != 200:
            print(f"  ERROR al crear usuario: {r.status_code} {r.text}")
            sys.exit(1)
        user_data = r.json()
        user_id = user_data.get("id", {}).get("id")
        print(f"  Usuario creado: {user_id}")

        # Activar usuario y set password
        print("[4/7] Configurando password...")
        r_act = await client.get(
            f"{TB_URL}/api/user/{user_id}/activationLink",
            headers=headers,
        )
        if r_act.status_code == 200:
            link = r_act.text.strip().strip('"')
            from urllib.parse import urlparse, parse_qs
            qs = parse_qs(urlparse(link).query)
            activate_token = qs.get("activateToken", [""])[0]
            if activate_token:
                r_set = await client.post(
                    f"{TB_URL}/api/noauth/activate",
                    json={"activateToken": activate_token, "password": USER_PASSWORD},
                    headers={"Content-Type": "application/json"},
                )
                if r_set.status_code == 200:
                    print(f"  Password configurado para {USER_EMAIL}")
                else:
                    print(f"  Error al setear password: {r_set.status_code} {r_set.text}")
            else:
                print(f"  No se pudo extraer activateToken del link")
        else:
            print(f"  Error al obtener activation link: {r_act.status_code}")

        # 5. Encontrar el edge
        print("[5/7] Buscando Edge...")
        r = await client.get(
            f"{TB_URL}/api/tenant/edges?pageSize=100&page=0",
            headers=headers,
        )
        edges = r.json().get("data", [])
        edge_id = None
        for e in edges:
            if e.get("name") == EDGE_NAME:
                edge_id = e.get("id", {}).get("id")
                print(f"  Edge encontrado: {edge_id}")
                break
        if not edge_id:
            print(f"  ERROR: Edge '{EDGE_NAME}' no encontrado")
            sys.exit(1)

        # 6. Asignar edge al customer
        print("[6/7] Asignando Edge al Customer...")
        r = await client.post(
            f"{TB_URL}/api/customer/{customer_id}/edge/{edge_id}",
            headers=headers,
        )
        if r.status_code == 200:
            print("  Edge asignado al customer")
        else:
            print(f"  Aviso: {r.status_code} (puede ya estar asignado)")

        # 7. Asignar todos los devices del edge al customer
        print("[7/7] Asignando Devices del edge al Customer...")
        r = await client.get(
            f"{TB_URL}/api/edge/{edge_id}/devices?pageSize=100&page=0",
            headers=headers,
        )
        devices = r.json().get("data", [])
        for dev in devices:
            did = dev.get("id", {}).get("id")
            name = dev.get("name", "?")
            r2 = await client.post(
                f"{TB_URL}/api/customer/{customer_id}/device/{did}",
                headers=headers,
            )
            if r2.status_code == 200:
                print(f"  Device '{name}' asignado")
            else:
                print(f"  Device '{name}': {r2.status_code} (puede ya estar asignado)")

        print("\n--- CONFIGURACION COMPLETA ---")
        print(f"  Customer: {CUSTOMER_TITLE} ({customer_id})")
        print(f"  Usuario:   {USER_EMAIL}")
        print(f"  Password:  {USER_PASSWORD}")
        print(f"  Edge:      {EDGE_NAME} ({edge_id})")
        print("\nLogin en: http://localhost:3000/login")


if __name__ == "__main__":
    asyncio.run(main())
