import httpx

from app.config import TB_BASE_URL


class TBClient:
    def __init__(self, base_url: str = TB_BASE_URL):
        self.base_url = base_url
        self._client = httpx.AsyncClient(timeout=30.0)

    async def login(self, username: str, password: str) -> str | None:
        resp = await self._client.post(
            f"{self.base_url}/api/auth/login",
            json={"username": username, "password": password},
        )
        if resp.status_code == 200:
            return resp.json().get("token")
        return None

    async def _headers(self, token: str) -> dict:
        return {"X-Authorization": f"Bearer {token}", "Content-Type": "application/json"}

    async def get_current_user(self, token: str) -> dict:
        headers = await self._headers(token)
        resp = await self._client.get(
            f"{self.base_url}/api/auth/user",
            headers=headers,
        )
        if resp.status_code == 200:
            data = resp.json()
            return {
                "authority": data.get("authority", "CUSTOMER_USER"),
                "customer_id": (data.get("customerId") or {}).get("id", ""),
                "first_name": data.get("firstName", ""),
                "last_name": data.get("lastName", ""),
                "email": data.get("email", ""),
            }
        return {"authority": "CUSTOMER_USER", "customer_id": "", "first_name": "", "last_name": "", "email": ""}

    async def get_tenant_devices(self, token: str, page_size: int = 250) -> list[dict]:
        headers = await self._headers(token)
        resp = await self._client.get(
            f"{self.base_url}/api/tenant/devices",
            params={"pageSize": page_size, "page": 0},
            headers=headers,
        )
        if resp.status_code == 200:
            return resp.json().get("data", [])
        return []

    async def get_customer_devices(self, token: str, customer_id: str, page_size: int = 250) -> list[dict]:
        headers = await self._headers(token)
        resp = await self._client.get(
            f"{self.base_url}/api/customer/{customer_id}/devices",
            params={"pageSize": page_size, "page": 0},
            headers=headers,
        )
        if resp.status_code == 200:
            return resp.json().get("data", [])
        return []

    async def get_tenant_edges(self, token: str, page_size: int = 50) -> list[dict]:
        headers = await self._headers(token)
        resp = await self._client.get(
            f"{self.base_url}/api/tenant/edges",
            params={"pageSize": page_size, "page": 0},
            headers=headers,
        )
        if resp.status_code == 200:
            return resp.json().get("data", [])
        return []

    async def get_customer_edges(self, token: str, customer_id: str, page_size: int = 50) -> list[dict]:
        headers = await self._headers(token)
        resp = await self._client.get(
            f"{self.base_url}/api/customer/{customer_id}/edges",
            params={"pageSize": page_size, "page": 0},
            headers=headers,
        )
        if resp.status_code == 200:
            return resp.json().get("data", [])
        return []

    async def get_device_attributes(self, token: str, device_id: str) -> dict:
        headers = await self._headers(token)
        resp = await self._client.get(
            f"{self.base_url}/api/plugins/telemetry/DEVICE/{device_id}/values/attributes",
            headers=headers,
        )
        if resp.status_code == 200:
            data = resp.json()
            return {item["key"]: item["value"] for item in data}
        return {}

    async def get_latest_telemetry(self, token: str, device_id: str, keys: str = "") -> dict:
        headers = await self._headers(token)
        params = {}
        if keys:
            params["keys"] = keys
        resp = await self._client.get(
            f"{self.base_url}/api/plugins/telemetry/DEVICE/{device_id}/values/timeseries",
            params=params,
            headers=headers,
        )
        if resp.status_code == 200:
            data = resp.json()
            result = {}
            latest_ts = 0
            for key, values in data.items():
                if values:
                    result[key] = values[0]["value"]
                    ts = int(values[0].get("ts", 0))
                    if ts > latest_ts:
                        latest_ts = ts
            if latest_ts > 0:
                result["_ts"] = str(latest_ts)
            return result
        return {}

    async def get_telemetry_history(
        self,
        token: str,
        device_id: str,
        keys: str,
        start_ts: int,
        end_ts: int,
        agg: str = "",
        interval: int = 0,
        limit: int = 1000,
    ) -> dict:
        headers = await self._headers(token)
        params: dict = {
            "keys": keys,
            "startTs": start_ts,
            "endTs": end_ts,
            "limit": limit,
        }
        if agg:
            params["agg"] = agg
        if interval and interval > 0:
            params["interval"] = interval
        resp = await self._client.get(
            f"{self.base_url}/api/plugins/telemetry/DEVICE/{device_id}/values/timeseries",
            params=params,
            headers=headers,
        )
        if resp.status_code == 200:
            return resp.json()
        return {}

    async def create_device(self, token: str, name: str, device_type: str = "sensor_sed", label: str = "") -> dict:
        headers = await self._headers(token)
        body = {"name": name, "type": device_type}
        if label:
            body["label"] = label
        resp = await self._client.post(
            f"{self.base_url}/api/device",
            json=body,
            headers=headers,
        )
        if resp.status_code == 200:
            return resp.json()
        return {}

    async def set_server_attributes(self, token: str, device_id: str, attributes: dict) -> bool:
        headers = await self._headers(token)
        resp = await self._client.post(
            f"{self.base_url}/api/plugins/telemetry/DEVICE/{device_id}/SERVER_SCOPE",
            json=attributes,
            headers=headers,
        )
        return resp.status_code == 200

    async def create_relation(
        self,
        token: str,
        from_id: str,
        from_type: str,
        to_id: str,
        to_type: str,
        relation_type: str = "Manages",
    ) -> bool:
        headers = await self._headers(token)
        resp = await self._client.post(
            f"{self.base_url}/api/relation",
            json={
                "from": {"id": from_id, "entityType": from_type},
                "to": {"id": to_id, "entityType": to_type},
                "type": relation_type,
                "typeGroup": "COMMON",
            },
            headers=headers,
        )
        return resp.status_code == 200

    async def assign_device_to_edge(self, token: str, edge_id: str, device_id: str) -> bool:
        headers = await self._headers(token)
        resp = await self._client.post(
            f"{self.base_url}/api/edge/{edge_id}/device/{device_id}",
            headers=headers,
        )
        return resp.status_code == 200

    async def assign_device_to_customer(self, token: str, customer_id: str, device_id: str) -> bool:
        headers = await self._headers(token)
        resp = await self._client.post(
            f"{self.base_url}/api/customer/{customer_id}/device/{device_id}",
            headers=headers,
        )
        return resp.status_code == 200

    async def get_device_credentials(self, token: str, device_id: str) -> str:
        headers = await self._headers(token)
        resp = await self._client.get(
            f"{self.base_url}/api/device/{device_id}/credentials",
            headers=headers,
        )
        if resp.status_code == 200:
            return resp.json().get("credentialsId", "")
        return ""

    async def send_rpc(self, token: str, device_id: str, method: str, params: dict) -> bool:
        headers = await self._headers(token)
        resp = await self._client.post(
            f"{self.base_url}/api/plugins/rpc/oneway/{device_id}",
            json={"method": method, "params": params},
            headers=headers,
        )
        return resp.status_code == 200

    async def delete_device(self, token: str, device_id: str) -> bool:
        headers = await self._headers(token)
        resp = await self._client.delete(
            f"{self.base_url}/api/device/{device_id}",
            headers=headers,
        )
        return resp.status_code == 200

    async def close(self):
        await self._client.aclose()
