import jwt
from datetime import datetime, timedelta, timezone
from fastapi import Request, HTTPException

from app.config import APP_SECRET


def create_session_token(username: str) -> str:
    payload = {
        "username": username,
        "exp": datetime.now(timezone.utc) + timedelta(hours=24),
    }
    return jwt.encode(payload, APP_SECRET, algorithm="HS256")


def decode_session_token(token: str) -> dict:
    try:
        return jwt.decode(token, APP_SECRET, algorithms=["HS256"])
    except jwt.PyJWTError:
        return {}


async def require_session(request: Request) -> str:
    token = request.cookies.get("session_token")
    if not token:
        raise HTTPException(status_code=401, detail="No autenticado")
    user = decode_session_token(token)
    if not user:
        raise HTTPException(status_code=401, detail="Sesion invalida o expirada")
    return token
