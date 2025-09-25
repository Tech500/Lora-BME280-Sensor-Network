# LoRa Environmental Sensor Network - Docker Container
# Base image with Python
FROM python:3.11-slim

# Set working directory
WORKDIR /app

# Install system dependencies
RUN apt-get update && apt-get install -y \
    sqlite3 \
    curl \
    && rm -rf /var/lib/apt/lists/*

# Create directories for data and config
RUN mkdir -p /app/data /app/config /app/logs

# Copy requirements first (for better caching)
COPY requirements.txt ./

# Install Python dependencies
RUN pip install --no-cache-dir -r requirements.txt

# Copy application code
COPY . .

# Create static directory and copy dashboard
# Create static directory and copy dashboard
RUN mkdir -p /app/static
RUN cp dashboard.html /app/static/dashboard.html
# Download chart.js library for dashboard charts
RUN apt-get update && apt-get install -y wget && \
    wget https://cdn.jsdelivr.net/npm/chart.js/dist/chart.umd.js -O /app/static/chart.udm.js && \
    apt-get remove -y wget && \
    rm -rf /var/lib/apt/lists/*

# Create default config if none exists
RUN echo '{"nodes": [{"id": "1001", "name": "Basement"}, {"id": "1002", "name": "Attic"}, {"id": "1003", "name": "Garage"}]}' > /app/config/nodes.json.default

# Create startup script
RUN echo '#!/bin/bash\n\
# Initialize database if it doesn'\''t exist\n\
if [ ! -f /app/data/sensors.db ]; then\n\
  echo "Initializing database..."\n\
  python init_db.py\n\
fi\n\
\n\
# Copy default config if none exists\n\
if [ ! -f /app/config/nodes.json ]; then\n\
  cp /app/config/nodes.json.default /app/config/nodes.json\n\
fi\n\
\n\
echo "Starting LoRa Sensor API..."\n\
python app.py' > /app/start.sh

RUN chmod +x /app/start.sh

# Expose API port
EXPOSE 5001

# Health check
HEALTHCHECK --interval=30s --timeout=3s --start-period=5s --retries=3 \
  CMD curl -f http://localhost:5001/health || exit 1

# Set environment variables
ENV PYTHONUNBUFFERED=1
ENV FLASK_APP=app.py
ENV FLASK_ENV=production
ENV DATABASE_PATH=/app/data/lora_sensors.db
ENV CONFIG_PATH=/app/config/settings.json
ENV DB_PATH=/app/data/lora_sensors.db
ENV LOG_LEVEL=info

# Run as non-root user for security
RUN addgroup --system --gid 1001 appuser && \
    adduser --system --uid 1001 --gid 1001 appuser
RUN chown -R appuser:appuser /app
USER appuser

# Start the application

CMD ["/app/start.sh"]

