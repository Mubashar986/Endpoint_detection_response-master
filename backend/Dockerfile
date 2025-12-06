# ===========================================
# DOCKERFILE FOR EDR BACKEND
# ===========================================

# BASE IMAGE: Python 3.11 on slim Debian (smaller size)
FROM python:3.11-slim

# Set environment variables
ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    PIP_NO_CACHE_DIR=1

# Set working directory
WORKDIR /app

# Install system dependencies
# - gcc, libffi-dev: For compiling some Python packages
# - netcat-openbsd: For health checks
RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc \
    libffi-dev \
    netcat-openbsd \
    && rm -rf /var/lib/apt/lists/*

# Copy requirements first (for Docker layer caching)
COPY requirements.txt .

# Install Python dependencies
RUN pip install --upgrade pip && \
    pip install -r requirements.txt

# Add channels and daphne for WebSocket support
RUN pip install channels channels-redis daphne zstandard

# Copy the rest of the application
COPY . .

# Expose port
EXPOSE 8000

# Default command: Run Daphne (ASGI server for WebSocket)
CMD ["daphne", "-b", "0.0.0.0", "-p", "8000", "edr_server.asgi:application"]
