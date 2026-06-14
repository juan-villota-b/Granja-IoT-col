import os

TB_HOST = os.getenv("TB_HOST", "localhost")
TB_PORT = int(os.getenv("TB_PORT", "8080"))
TB_BASE_URL = f"http://{TB_HOST}:{TB_PORT}"

APP_SECRET = os.getenv("APP_SECRET", "granja-dashboard-secret-key-change-in-production")
