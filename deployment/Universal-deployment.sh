#!/bin/bash

# LoRa BME280 Sensor Network - Universal Deployment Script
# Supports: Docker (Pi/x86), Cloud VPS, Native Pi, Development
# Usage: ./deploy.sh [target] [environment] [options]

set -e  # Exit on any error

# Version
VERSION="1.0.0"
PROJECT_NAME="lora-bme280-network"

# Default configuration
DEFAULT_TARGET="docker"
DEFAULT_ENV="production"
DEFAULT_PORT="8080"
DEFAULT_BRANCH="main"

# Parse command line arguments
TARGET="${1:-$DEFAULT_TARGET}"
ENVIRONMENT="${2:-$DEFAULT_ENV}"
PORT="${PORT:-$DEFAULT_PORT}"
BRANCH="${BRANCH:-$DEFAULT_BRANCH}"
DOMAIN="${DOMAIN:-localhost}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
NC='\033[0m'

# Logging functions
log() {
    echo -e "${GREEN}[$(date +'%H:%M:%S')] $1${NC}"
}

info() {
    echo -e "${BLUE}[$(date +'%H:%M:%S')] INFO: $1${NC}"
}

warn() {
    echo -e "${YELLOW}[$(date +'%H:%M:%S')] WARNING: $1${NC}"
}

error() {
    echo -e "${RED}[$(date +'%H:%M:%S')] ERROR: $1${NC}"
}

success() {
    echo -e "${PURPLE}[$(date +'%H:%M:%S')] ✓ $1${NC}"
}

# Print banner
print_banner() {
    echo -e "${PURPLE}"
    echo "╔════════════════════════════════════════════════╗"
    echo "║          LoRa BME280 Sensor Network            ║"
    echo "║              Deployment Script                 ║"
    echo "║                 v${VERSION}                        ║"
    echo "╚════════════════════════════════════════════════╝"
    echo -e "${NC}"
    info "Target: $TARGET | Environment: $ENVIRONMENT | Port: $PORT"
    echo ""
}

# Detect system architecture and OS
detect_system() {
    ARCH=$(uname -m)
    OS=$(uname -s)
    
    case $ARCH in
        armv6l|armv7l) PLATFORM="linux/arm/v7" ;;
        aarch64) PLATFORM="linux/arm64" ;;
        x86_64) PLATFORM="linux/amd64" ;;
        *) PLATFORM="linux/amd64" ;;
    esac
    
    # Detect if running on Raspberry Pi
    if [ -f /proc/device-tree/model ] && grep -q "Raspberry Pi" /proc/device-tree/model; then
        IS_PI=true
        PI_MODEL=$(tr -d '\0' < /proc/device-tree/model)
        info "Detected: $PI_MODEL"
    else
        IS_PI=false
        info "Detected: $OS $ARCH"
    fi
    
    info "Docker platform: $PLATFORM"
}

# Check prerequisites
check_prerequisites() {
    log "Checking prerequisites..."
    
    case $TARGET in
        "docker"|"docker-pi")
            if ! command -v docker &> /dev/null; then
                error "Docker is not installed"
                info "Install Docker with: curl -fsSL https://get.docker.com -o get-docker.sh && sh get-docker.sh"
                exit 1
            fi
            
            if ! command -v docker-compose &> /dev/null; then
                warn "Docker Compose not found, installing..."
                install_docker_compose
            fi
            ;;
        "cloud"|"vps")
            if [[ $EUID -ne 0 ]]; then
                error "Cloud deployment requires root privileges (use sudo)"
                exit 1
            fi
            ;;
        "native"|"pi")
            if [ "$IS_PI" = false ] && [ "$TARGET" = "pi" ]; then
                warn "Target is 'pi' but not running on Raspberry Pi"
            fi
            ;;
    esac
    
    success "Prerequisites check passed"
}

# Install Docker Compose
install_docker_compose() {
    log "Installing Docker Compose..."
    
    if [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "armv7l" ]; then
        # For ARM architectures, use pip installation
        sudo apt-get update
        sudo apt-get install -y python3-pip
        sudo pip3 install docker-compose
    else
        # For x86_64, use official binary
        sudo curl -L "https://github.com/docker/compose/releases/latest/download/docker-compose-$(uname -s)-$(uname -m)" -o /usr/local/bin/docker-compose
        sudo chmod +x /usr/local/bin/docker-compose
    fi
    
    success "Docker Compose installed"
}

# Generate Docker Compose configuration
generate_docker_compose() {
    log "Generating Docker Compose configuration..."
    
    cat > docker-compose.yml << EOF
version: '3.8'

services:
  lora-sensor:
    build: 
      context: .
      dockerfile: Dockerfile
      platforms:
        - $PLATFORM
    container_name: ${PROJECT_NAME}
    restart: unless-stopped
    ports:
      - "${PORT}:${PORT}"
    environment:
      - ENVIRONMENT=${ENVIRONMENT}
      - PORT=${PORT}
      - LORA_FREQUENCY=868.0
      - LORA_BANDWIDTH=125000
      - LORA_SPREADING_FACTOR=7
      - BME280_ADDRESS=0x76
      - DATABASE_URL=sqlite:///data/sensors.db
      - LOG_LEVEL=INFO
    volumes:
      - ./data:/app/data
      - ./logs:/app/logs
      - ./config:/app/config
    devices:
      # Uncomment if using physical LoRa hardware
      # - "/dev/spidev0.0:/dev/spidev0.0"
      # - "/dev/gpiomem:/dev/gpiomem"
    privileged: false
    networks:
      - lora-network
    healthcheck:
      test: ["CMD", "curl", "-f", "http://localhost:${PORT}/health"]
      interval: 30s
      timeout: 10s
      retries: 3
      start_period: 60s

  # SQLite3 database - no separate container needed, data stored in volume

  redis:
    image: redis:7-alpine
    container_name: ${PROJECT_NAME}-redis
    restart: unless-stopped
    volumes:
      - redis_data:/data
    networks:
      - lora-network
    command: redis-server --appendonly yes

  nginx:
    image: nginx:alpine
    container_name: ${PROJECT_NAME}-nginx
    restart: unless-stopped
    ports:
      - "80:80"
      - "443:443"
    volumes:
      - ./nginx.conf:/etc/nginx/nginx.conf:ro
      - ./ssl:/etc/ssl/certs:ro
    depends_on:
      - lora-sensor
    networks:
      - lora-network

networks:
  lora-network:
    driver: bridge

volumes:
  redis_data:
EOF

    success "Docker Compose configuration generated"
}

# Generate Dockerfile
generate_dockerfile() {
    log "Generating Dockerfile..."
    
    cat > Dockerfile << EOF
# Multi-stage build for LoRa BME280 Sensor Network
FROM python:3.9-slim-bullseye as builder

# Install build dependencies
RUN apt-get update && apt-get install -y \\
    build-essential \\
    gcc \\
    g++ \\
    libc6-dev \\
    python3-dev \\
    && rm -rf /var/lib/apt/lists/*

# Create virtual environment
RUN python -m venv /opt/venv
ENV PATH="/opt/venv/bin:\$PATH"

# Copy requirements and install Python dependencies
COPY requirements.txt .
RUN pip install --upgrade pip && \\
    pip install --no-cache-dir -r requirements.txt

# Production stage
FROM python:3.9-slim-bullseye

# Install runtime dependencies
RUN apt-get update && apt-get install -y \\
    curl \\
    i2c-tools \\
    && rm -rf /var/lib/apt/lists/* \\
    && groupadd -r appuser \\
    && useradd -r -g appuser appuser

# Copy virtual environment from builder
COPY --from=builder /opt/venv /opt/venv
ENV PATH="/opt/venv/bin:\$PATH"

# Set working directory
WORKDIR /app

# Copy application code
COPY . .

# Create necessary directories
RUN mkdir -p /app/data /app/logs /app/config && \\
    chown -R appuser:appuser /app

# Switch to non-root user
USER appuser

# Expose port
EXPOSE \${PORT:-8080}

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=60s --retries=3 \\
    CMD curl -f http://localhost:\${PORT:-8080}/health || exit 1

# Start application
CMD ["python", "app.py"]
EOF

    success "Dockerfile generated"
}

# Generate Nginx configuration
generate_nginx_config() {
    log "Generating Nginx configuration..."
    
    cat > nginx.conf << EOF
events {
    worker_connections 1024;
}

http {
    upstream lora_backend {
        server lora-sensor:${PORT};
    }

    server {
        listen 80;
        server_name ${DOMAIN};

        location / {
            proxy_pass http://lora_backend;
            proxy_set_header Host \$host;
            proxy_set_header X-Real-IP \$remote_addr;
            proxy_set_header X-Forwarded-For \$proxy_add_x_forwarded_for;
            proxy_set_header X-Forwarded-Proto \$scheme;
            proxy_connect_timeout 60s;
            proxy_send_timeout 60s;
            proxy_read_timeout 60s;
        }

        location /health {
            proxy_pass http://lora_backend/health;
            access_log off;
        }
    }
}
EOF

    success "Nginx configuration generated"
}

# Generate sample application files
generate_sample_files() {
    if [ ! -f "requirements.txt" ]; then
        log "Generating sample requirements.txt..."
        cat > requirements.txt << EOF
Flask==2.3.2
Flask-CORS==4.0.0
requests==2.31.0
python-dotenv==1.0.0
schedule==1.2.0
redis==4.6.0
gunicorn==21.2.0
# Database - SQLite3 (built into Python, no extra packages needed)
# LoRa and sensor libraries
pyserial==3.5
spidev==3.6
RPi.GPIO==0.7.1; sys_platform == "linux"
adafruit-circuitpython-bme280==3.2.27
paho-mqtt==1.6.1
EOF
    fi

    if [ ! -f "app.py" ]; then
        log "Generating sample app.py..."
        cat > app.py << EOF
#!/usr/bin/env python3
"""
LoRa BME280 Sensor Network - Main Application
"""

import os
import time
import sqlite3
from flask import Flask, jsonify, request
from flask_cors import CORS

app = Flask(__name__)
CORS(app)

# Configuration
PORT = int(os.getenv('PORT', 8080))
ENVIRONMENT = os.getenv('ENVIRONMENT', 'development')
DATABASE_PATH = os.getenv('DATABASE_PATH', 'data/sensors.db')

def init_database():
    """Initialize SQLite database with sensor tables"""
    os.makedirs(os.path.dirname(DATABASE_PATH), exist_ok=True)
    
    conn = sqlite3.connect(DATABASE_PATH)
    cursor = conn.cursor()
    
    # Create sensors table
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS sensors (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            sensor_id TEXT NOT NULL,
            temperature REAL,
            humidity REAL,
            pressure REAL,
            battery_level REAL,
            rssi INTEGER,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP
        )
    ''')
    
    # Create sensor_nodes table
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS sensor_nodes (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            node_id TEXT UNIQUE NOT NULL,
            name TEXT,
            location TEXT,
            last_seen DATETIME,
            is_active BOOLEAN DEFAULT 1,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP
        )
    ''')
    
    conn.commit()
    conn.close()

def get_db_connection():
    """Get database connection"""
    conn = sqlite3.connect(DATABASE_PATH)
    conn.row_factory = sqlite3.Row  # Enable column access by name
    return conn

@app.route('/')
def home():
    return jsonify({
        'message': 'LoRa BME280 Sensor Network',
        'version': '1.0.0',
        'environment': ENVIRONMENT,
        'database': 'SQLite3',
        'timestamp': time.time()
    })

@app.route('/health')
def health():
    try:
        # Test database connection
        conn = get_db_connection()
        conn.execute('SELECT 1').fetchone()
        conn.close()
        db_status = 'healthy'
    except Exception as e:
        db_status = f'error: {str(e)}'
    
    return jsonify({
        'status': 'healthy',
        'database': db_status,
        'timestamp': time.time()
    })

@app.route('/api/sensors')
def get_sensors():
    """Get recent sensor readings"""
    try:
        conn = get_db_connection()
        cursor = conn.cursor()
        
        # Get latest readings from each sensor
        cursor.execute('''
            SELECT sensor_id, temperature, humidity, pressure, 
                   battery_level, rssi, timestamp
            FROM sensors 
            WHERE timestamp > datetime('now', '-1 hour')
            ORDER BY timestamp DESC 
            LIMIT 50
        ''')
        
        readings = []
        for row in cursor.fetchall():
            readings.append({
                'sensor_id': row['sensor_id'],
                'temperature': row['temperature'],
                'humidity': row['humidity'], 
                'pressure': row['pressure'],
                'battery_level': row['battery_level'],
                'rssi': row['rssi'],
                'timestamp': row['timestamp']
            })
        
        conn.close()
        
        return jsonify({
            'sensors': readings,
            'count': len(readings)
        })
        
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/sensors', methods=['POST'])
def add_sensor_reading():
    """Add new sensor reading"""
    try:
        data = request.get_json()
        
        conn = get_db_connection()
        cursor = conn.cursor()
        
        cursor.execute('''
            INSERT INTO sensors (sensor_id, temperature, humidity, pressure, 
                               battery_level, rssi)
            VALUES (?, ?, ?, ?, ?, ?)
        ''', (
            data.get('sensor_id'),
            data.get('temperature'),
            data.get('humidity'),
            data.get('pressure'),
            data.get('battery_level'),
            data.get('rssi')
        ))
        
        conn.commit()
        conn.close()
        
        return jsonify({'status': 'success', 'message': 'Reading saved'}), 201
        
    except Exception as e:
        return jsonify({'error': str(e)}), 500

if __name__ == '__main__':
    print(f"Starting LoRa BME280 Sensor Network on port {PORT}")
    print(f"Environment: {ENVIRONMENT}")
    print(f"Database: {DATABASE_PATH}")
    
    # Initialize database
    init_database()
    
    app.run(host='0.0.0.0', port=PORT, debug=(ENVIRONMENT == 'development'))
EOF
    fi

    success "Sample application files generated"
}

# Create directory structure
create_directories() {
    log "Creating directory structure..."
    
    mkdir -p data logs config ssl
    touch data/.gitkeep logs/.gitkeep config/.gitkeep ssl/.gitkeep
    
    success "Directory structure created"
}

# Docker deployment
deploy_docker() {
    log "Deploying with Docker..."
    
    # Stop existing containers
    if [ -f "docker-compose.yml" ]; then
        docker-compose down 2>/dev/null || true
    fi
    
    generate_docker_compose
    generate_dockerfile
    generate_nginx_config
    generate_sample_files
    create_directories
    
    # Build and start services
    log "Building and starting services..."
    docker-compose build --no-cache
    docker-compose up -d
    
    # Wait for services to be healthy
    log "Waiting for services to start..."
    sleep 30
    
    # Health check
    if curl -f http://localhost:$PORT/health &>/dev/null; then
        success "Docker deployment successful!"
    else
        warn "Health check failed, checking logs..."
        docker-compose logs --tail=20
    fi
}

# Cloud/VPS deployment
deploy_cloud() {
    log "Deploying to cloud/VPS..."
    
    # Update system
    apt-get update
    apt-get install -y curl wget git docker.io docker-compose sqlite3
    
    # Enable Docker service
    systemctl enable docker
    systemctl start docker
    
    # Add current user to docker group if not root
    if [ "$SUDO_USER" ]; then
        usermod -aG docker $SUDO_USER
    fi
    
    # Deploy using Docker
    deploy_docker
    
    # Setup firewall
    if command -v ufw &> /dev/null; then
        ufw allow 22/tcp
        ufw allow 80/tcp
        ufw allow 443/tcp
        ufw allow $PORT/tcp
        ufw --force enable
    fi
    
    success "Cloud deployment completed!"
}

# Native Pi deployment
deploy_native() {
    log "Deploying natively on Raspberry Pi..."
    
    # Update system
    sudo apt-get update
    sudo apt-get install -y python3 python3-pip python3-venv git i2c-tools sqlite3
    
    # Enable I2C and SPI
    if ! grep -q "dtparam=i2c_arm=on" /boot/config.txt; then
        echo "dtparam=i2c_arm=on" | sudo tee -a /boot/config.txt
        warn "I2C enabled - reboot required"
    fi
    
    # Create Python virtual environment
    python3 -m venv venv
    source venv/bin/activate
    pip install --upgrade pip
    
    generate_sample_files
    pip install -r requirements.txt
    
    # Create data directory
    mkdir -p data
    
    # Create systemd service
    cat > /tmp/${PROJECT_NAME}.service << EOF
[Unit]
Description=LoRa BME280 Sensor Network
After=network.target

[Service]
Type=simple
User=$USER
WorkingDirectory=$(pwd)
Environment=PATH=$(pwd)/venv/bin
Environment=DATABASE_PATH=$(pwd)/data/sensors.db
ExecStart=$(pwd)/venv/bin/python app.py
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
EOF

    sudo mv /tmp/${PROJECT_NAME}.service /etc/systemd/system/
    sudo systemctl daemon-reload
    sudo systemctl enable ${PROJECT_NAME}
    sudo systemctl start ${PROJECT_NAME}
    
    success "Native deployment completed!"
}

# Development setup
setup_development() {
    log "Setting up development environment..."
    
    generate_sample_files
    create_directories
    
    # Create Python virtual environment
    python3 -m venv venv
    source venv/bin/activate
    pip install --upgrade pip
    pip install -r requirements.txt
    
    # Create development configuration
    cat > .env << EOF
ENVIRONMENT=development
PORT=${PORT}
DEBUG=true
LOG_LEVEL=DEBUG
EOF

    success "Development environment ready!"
    info "To start development server:"
    info "  source venv/bin/activate"
    info "  python app.py"
}

# Show deployment status
show_status() {
    log "Deployment Status"
    echo "=================="
    
    case $TARGET in
        "docker"|"docker-pi"|"cloud")
            if command -v docker-compose &> /dev/null && [ -f "docker-compose.yml" ]; then
                docker-compose ps
                echo ""
                info "Application URL: http://$DOMAIN:$PORT"
                info "Health Check: http://$DOMAIN:$PORT/health"
                info "API Endpoint: http://$DOMAIN:$PORT/api/sensors"
                echo ""
                info "Useful Commands:"
                echo "  View logs: docker-compose logs -f"
                echo "  Restart: docker-compose restart"
                echo "  Stop: docker-compose down"
                echo "  Rebuild: docker-compose build --no-cache"
            fi
            ;;
        "native"|"pi")
            if systemctl is-active --quiet ${PROJECT_NAME}; then
                success "Service is running"
                systemctl status ${PROJECT_NAME} --no-pager -l
            else
                warn "Service is not running"
            fi
            ;;
        "development")
            info "Development environment configured"
            info "Start with: source venv/bin/activate && python app.py"
            ;;
    esac
}

# Main deployment function
main() {
    print_banner
    detect_system
    check_prerequisites
    
    case $TARGET in
        "docker"|"docker-pi")
            deploy_docker
            ;;
        "cloud"|"vps")
            deploy_cloud
            ;;
        "native"|"pi")
            deploy_native
            ;;
        "dev"|"development")
            setup_development
            ;;
        *)
            error "Unknown target: $TARGET"
            show_help
            exit 1
            ;;
    esac
    
    echo ""
    show_status
    success "Deployment completed successfully!"
}

# Show help
show_help() {
    echo "LoRa BME280 Sensor Network - Deployment Script v$VERSION"
    echo ""
    echo "Usage: $0 [target] [environment] [options]"
    echo ""
    echo "Targets:"
    echo "  docker      - Docker containers (default)"
    echo "  docker-pi   - Docker on Raspberry Pi"
    echo "  cloud       - Cloud VPS with Docker"
    echo "  native      - Native installation on Pi"
    echo "  development - Local development setup"
    echo ""
    echo "Environments:"
    echo "  production  - Production deployment (default)"
    echo "  staging     - Staging environment"
    echo "  development - Development environment"
    echo ""
    echo "Environment Variables:"
    echo "  PORT=8080           - Application port"
    echo "  DOMAIN=localhost    - Domain name"
    echo "  BRANCH=main         - Git branch to deploy"
    echo ""
    echo "Examples:"
    echo "  $0                              # Docker deployment"
    echo "  $0 docker-pi production         # Docker on Pi"
    echo "  $0 cloud production             # Cloud deployment"
    echo "  DOMAIN=mysensor.com $0 cloud    # Cloud with custom domain"
    echo "  $0 development                  # Development setup"
    echo ""
    echo "Other Commands:"
    echo "  $0 status   - Show deployment status"
    echo "  $0 logs     - Show application logs"
    echo "  $0 help     - Show this help"
}

# Handle special commands
case "${1:-}" in
    "help"|"-h"|"--help")
        show_help
        exit 0
        ;;
    "status")
        show_status
        exit 0
        ;;
    "logs")
        if [ -f "docker-compose.yml" ]; then
            docker-compose logs -f
        else
            echo "No Docker Compose configuration found"
            exit 1
        fi
        ;;
    "version"|"-v"|"--version")
        echo "LoRa BME280 Sensor Network Deploy Script v$VERSION"
        exit 0
        ;;
    *)
        main "$@"
        ;;
esac