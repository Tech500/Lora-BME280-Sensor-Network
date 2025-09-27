FROM python:3.9-slim

WORKDIR /app

# Create necessary directories
RUN mkdir -p /app/data /app/config /app/static /app/templates &&     chmod 755 /app/data /app/config /app/static /app/templates

# Install system dependencies
RUN apt-get update && apt-get install -y     gcc     && rm -rf /var/lib/apt/lists/*

# Copy requirements first for better caching
COPY requirements.txt .

# Install Python dependencies
RUN pip install --no-cache-dir -r requirements.txt

# Copy the app.py from the app/ subdirectory to /app/
COPY app/app.py .

# Copy static files
COPY static/ ./static/

# Copy config files
COPY config/ ./config/

# Expose port
EXPOSE 5001

# Run the application
CMD ["python", "app.py"]
