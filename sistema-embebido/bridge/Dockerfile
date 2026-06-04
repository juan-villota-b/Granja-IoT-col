FROM python:3.13-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    iproute2 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY . .

EXPOSE 15683 15684 15685 15686 15687

COPY start.sh /start.sh
RUN chmod +x /start.sh

CMD ["/start.sh"]
