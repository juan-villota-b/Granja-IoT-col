import httpx
from typing import Optional, Any
from datetime import datetime

from app.config import TB_BASE_URL


class TBClient:
    def __init__(self, base_url: str = TB_BASE_URL):
        self.base_url = base_url
        self._client = httpx.AsyncClient(timeout=30.0)

    async def login(self, username: str, password: str) -> Optional[str]:
        resp = await self._client.post(
            f"{self.base_url}/api/auth/login",
            json={"username": username, "password": password},
        )
        if resp.status_code == 200:
            return resp.json().get("token")
        return None

    async def get_headers(self, token: str) -> dict:
        return {"X-Authorization": f"Bearer {token}", "Content-Type": "application/json"}

    async def get_tenant_devices(self, token: str, page_size: int = 100) -> list[dict]:
        headers = await self.get_headers(token)
        resp = await self._client.get(
            f"{self.base_url}/api/tenant/devices",
            params={"pageSize": page_size, "page": 0},
            headers=headers,
        )
        if resp.status_code == 200:
            return resp.json().get("data", [])
        return []

    async def get_device_attributes(self, token: str, device_id: str) -> dict:
        headers = await self.get_headers(token)
        resp = await self._client.get(
            f"{self.base_url}/api/plugins/telemetry/DEVICE/{device_id}/values/attributes",
            headers=headers,
        )
        if resp.status_code == 200:
            data = resp.json()
            return {item["key"]: item["value"] for item in data}
        return {}

    async def get_device_attributes_scope(self, token: str, device_id: str, scope: str = "SERVER_SCOPE") -> dict:
        headers = await self.get_headers(token)
        resp = await self._client.get(
            f"{self.base_url}/api/plugins/telemetry/DEVICE/{device_id}/values/attributes/{scope}",
            headers=headers,
        )
        if resp.status_code == 200:
            data = resp.json()
            return {item["key"]: item["value"] for item in data}
        return {}

    async def get_latest_telemetry(self, token: str, device_id: str, keys: str = "") -> dict:
        headers = await self.get_headers(token)
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
        agg: str = "AVG",
        interval: int = 3600000,
        limit: int = 1000,
    ) -> dict:
        headers = await self.get_headers(token)
        resp = await self._client.get(
            f"{self.base_url}/api/plugins/telemetry/DEVICE/{device_id}/values/timeseries",
            params={
                "keys": keys,
                "startTs": start_ts,
                "endTs": end_ts,
                "agg": agg,
                "interval": interval,
                "limit": limit,
            },
            headers=headers,
        )
        if resp.status_code == 200:
            return resp.json()
        return {}

    async def get_relations(
        self,
        token: str,
        from_id: str,
        from_type: str = "DEVICE",
        relation_type: str = "Manages",
    ) -> list[dict]:
        headers = await self.get_headers(token)
        resp = await self._client.get(
            f"{self.base_url}/api/relations",
            params={
                "fromId": from_id,
                "fromType": from_type,
                "relationTypeGroup": "COMMON",
                "relationType": relation_type,
            },
            headers=headers,
        )
        if resp.status_code == 200:
            return resp.json()
        return []

    async def get_device_by_id(self, token: str, device_id: str) -> dict:
        headers = await self.get_headers(token)
        resp = await self._client.get(
            f"{self.base_url}/api/device/{device_id}",
            headers=headers,
        )
        if resp.status_code == 200:
            return resp.json()
        return {}

    async def get_device_credentials(self, token: str, device_id: str) -> dict:
        headers = await self.get_headers(token)
        resp = await self._client.get(
            f"{self.base_url}/api/device/{device_id}/credentials",
            headers=headers,
        )
        if resp.status_code == 200:
            return resp.json()
        return {}

    async def send_rpc(self, token: str, device_id: str, method: str, params: dict) -> bool:
        headers = await self.get_headers(token)
        resp = await self._client.post(
            f"{self.base_url}/api/rpc",
            json={"method": method, "params": params},
            headers=headers,
        )
        return resp.status_code == 200

    async def close(self):
        await self._client.aclose()
