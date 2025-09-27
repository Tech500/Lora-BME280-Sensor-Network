FROM python:3.9-slim

WORKDIR /app

# Create necessary directories
RUN mkdir -p /app/data /app/config /app/static /app/templates && \
    chmod 755 /app/data /app/config /app/static /app/templates

# Install system dependencies
RUN apt-get update && apt-get install -y \
    gcc \
    && rm -rf /var/lib/apt/lists/*

# Copy requirements first for better caching
COPY requirements.txt .

# Install Python dependencies
RUN pip install --no-cache-dir -r requirements.txt

# Copy application files
COPY . .

# Copy default config file if it exists
COPY config/nodes.json.default /app/config/nodes.json.default || true

# Expose port
EXPOSE 5001

# Run the application
CMD ["python", "app.py"]
