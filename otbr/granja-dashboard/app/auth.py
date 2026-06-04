import jwt
from datetime import datetime, timedelta, timezone
from fastapi import Request, HTTPException
from fastapi.responses import RedirectResponse

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


async def get_current_user(request: Request) -> dict:
    token = request.cookies.get("session_token") or request.headers.get("X-Session-Token")
    if not token:
        raise HTTPException(status_code=401, detail="No autenticado")
    user = decode_session_token(token)
    if not user:
        raise HTTPException(status_code=401, detail="Sesión inválida o expirada")
    return user
