# LoRa BME280 Sensor Network - Universal Deployment Guide

A comprehensive deployment solution for LoRa-based BME280 sensor networks supporting multiple platforms and environments.

## 🚀 Quick Start

```bash
# Make script executable
chmod +x deploy.sh

# Docker deployment (recommended)
./deploy.sh docker production

# Raspberry Pi with Docker
./deploy.sh docker-pi production

# Development environment
./deploy.sh development
```

## 📋 Table of Contents

- [Overview](#overview)
- [Prerequisites](#prerequisites)
- [Deployment Targets](#deployment-targets)
- [Installation](#installation)
- [Configuration](#configuration)
- [Usage Examples](#usage-examples)
- [API Documentation](#api-documentation)
- [Troubleshooting](#troubleshooting)
- [Contributing](#contributing)

## 🔍 Overview

This universal deployment script automates the setup of a LoRa BME280 sensor network across different platforms:

- **Docker containers** (Pi/x86) with auto-architecture detection
- **Cloud VPS** deployment with full stack
- **Native Raspberry Pi** installation with hardware support
- **Development environment** setup

### Features

- ✅ Multi-platform support (ARM64, ARMv7, x86_64)
- ✅ SQLite3 database (lightweight, perfect for IoT)
- ✅ Auto-detection of Raspberry Pi hardware
- ✅ Complete Docker Compose stack
- ✅ Nginx reverse proxy with SSL support
- ✅ Health monitoring and logging
- ✅ Redis caching for performance
- ✅ Systemd service management
- ✅ Firewall configuration
- ✅ Development tools integration

## 📦 Prerequisites

### General Requirements
- Linux-based system (Ubuntu, Debian, Raspberry Pi OS)
- Bash shell
- Internet connection
- `curl` and `git` installed

### Docker Deployment
```bash
# Install Docker
curl -fsSL https://get.docker.com -o get-docker.sh
sh get-docker.sh

# Add user to docker group
sudo usermod -aG docker $USER
```

### Cloud/VPS Deployment
- Root access (`sudo` privileges)
- Open ports: 80, 443, 8080 (or custom port)

### Native Pi Deployment
- Raspberry Pi with I2C/SPI enabled
- Python 3.7+ installed

## 🎯 Deployment Targets

### 1. Docker (Recommended)

**Best for:** Development, testing, easy deployment

```bash
./deploy.sh docker production
```

**What it includes:**
- Application container with Flask API
- Redis container for caching
- Nginx container for reverse proxy
- SQLite3 database in persistent volume
- Health checks and auto-restart

### 2. Docker on Raspberry Pi

**Best for:** Production IoT deployments

```bash
./deploy.sh docker-pi production
```

**Additional features:**
- ARM architecture optimization
- Hardware device access configuration
- GPIO and SPI device mapping
- Power-efficient container settings

### 3. Cloud/VPS Deployment

**Best for:** Scalable cloud deployments

```bash
sudo DOMAIN=yourdomain.com ./deploy.sh cloud production
```

**What it includes:**
- Complete Docker stack
- UFW firewall configuration
- Let's Encrypt SSL certificates
- System service management
- Automated security hardening

### 4. Native Raspberry Pi

**Best for:** Direct hardware integration

```bash
./deploy.sh native production
```

**What it includes:**
- Python virtual environment
- I2C/SPI interface enabling
- Systemd service creation
- Direct hardware access
- Optimized for Pi performance

### 5. Development Environment

**Best for:** Local development and testing

```bash
./deploy.sh development
```

**What it includes:**
- Python virtual environment
- Development dependencies
- Debug logging enabled
- Hot reload configuration
- SQLite3 database setup

## 🔧 Installation

### Step 1: Clone Repository

```bash
git clone https://github.com/your-repo/lora-bme280-network.git
cd lora-bme280-network
```

### Step 2: Make Script Executable

```bash
chmod +x deploy.sh
```

### Step 3: Choose Deployment Method

See [Usage Examples](#usage-examples) below for specific deployment commands.

## ⚙️ Configuration

### Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `PORT` | `8080` | Application port |
| `DOMAIN` | `localhost` | Domain name for deployment |
| `BRANCH` | `main` | Git branch to deploy |
| `ENVIRONMENT` | `production` | Deployment environment |

### Example Configuration

```bash
# Custom port and domain
PORT=3000 DOMAIN=sensor.local ./deploy.sh docker-pi

# Staging environment
DOMAIN=staging.mysensors.com ./deploy.sh cloud staging

# Development with custom branch
BRANCH=feature-branch ./deploy.sh development
```

### Application Configuration

The script generates configuration files:

- `docker-compose.yml` - Docker services configuration
- `nginx.conf` - Reverse proxy settings
- `.env` - Environment variables
- `data/sensors.db` - SQLite3 database

## 📝 Usage Examples

### Basic Deployments

```bash
# Docker on any system
./deploy.sh docker production

# Docker on Raspberry Pi (auto-detected)
./deploy.sh docker-pi production

# Cloud VPS with SSL
sudo DOMAIN=api.mysensors.com ./deploy.sh cloud production

# Native Pi installation
./deploy.sh native production

# Development setup
./deploy.sh development
```

### Advanced Deployments

```bash
# Custom port and domain
PORT=9000 DOMAIN=sensors.local ./deploy.sh docker-pi

# Staging environment
./deploy.sh docker staging

# Development with specific branch
BRANCH=experimental ./deploy.sh development

# Cloud with custom configuration
sudo DOMAIN=prod.sensors.io PORT=8443 ./deploy.sh cloud production
```

### Management Commands

```bash
# Check deployment status
./deploy.sh status

# View application logs
./deploy.sh logs

# Show help
./deploy.sh help

# Show version
./deploy.sh version
```

## 📡 API Documentation

### Base URL
- Docker: `http://localhost:8080`
- Custom: `http://localhost:$PORT`
- Cloud: `https://yourdomain.com`

### Endpoints

#### GET /
Welcome message and system information
```json
{
  "message": "LoRa BME280 Sensor Network",
  "version": "1.0.0",
  "environment": "production",
  "database": "SQLite3",
  "timestamp": 1640995200.0
}
```

#### GET /health
Health check endpoint
```json
{
  "status": "healthy",
  "database": "healthy",
  "timestamp": 1640995200.0
}
```

#### GET /api/sensors
Get recent sensor readings
```json
{
  "sensors": [
    {
      "sensor_id": "node_001",
      "temperature": 22.5,
      "humidity": 65.2,
      "pressure": 1013.25,
      "battery_level": 3.7,
      "rssi": -45,
      "timestamp": "2023-12-31 12:00:00"
    }
  ],
  "count": 1
}
```

#### POST /api/sensors
Submit new sensor reading
```bash
curl -X POST http://localhost:8080/api/sensors \
  -H "Content-Type: application/json" \
  -d '{
    "sensor_id": "node_001",
    "temperature": 23.1,
    "humidity": 67.5,
    "pressure": 1012.8,
    "battery_level": 3.6,
    "rssi": -48
  }'
```

Response:
```json
{
  "status": "success",
  "message": "Reading saved"
}
```

## 🗄️ Database Schema

### SQLite3 Tables

#### sensors
Stores sensor readings
```sql
CREATE TABLE sensors (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    sensor_id TEXT NOT NULL,
    temperature REAL,
    humidity REAL,
    pressure REAL,
    battery_level REAL,
    rssi INTEGER,
    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);
```

#### sensor_nodes
Tracks sensor node information
```sql
CREATE TABLE sensor_nodes (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    node_id TEXT UNIQUE NOT NULL,
    name TEXT,
    location TEXT,
    last_seen DATETIME,
    is_active BOOLEAN DEFAULT 1,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);
```

### Database Access

```bash
# Connect to database
sqlite3 data/sensors.db

# View tables
.tables

# Query recent readings
SELECT * FROM sensors ORDER BY timestamp DESC LIMIT 10;

# Check sensor nodes
SELECT * FROM sensor_nodes WHERE is_active = 1;
```

## 🐳 Docker Services

### Service Overview

| Service | Port | Description |
|---------|------|-------------|
| lora-sensor | 8080 | Main Flask application |
| redis | 6379 | Caching and session storage |
| nginx | 80/443 | Reverse proxy and SSL |

### Docker Commands

```bash
# View running containers
docker-compose ps

# View logs
docker-compose logs -f lora-sensor

# Restart services
docker-compose restart

# Stop all services
docker-compose down

# Rebuild and start
docker-compose build --no-cache
docker-compose up -d

# Access container shell
docker-compose exec lora-sensor bash
```

### Volume Mounts

| Host Path | Container Path | Purpose |
|-----------|---------------|---------|
| `./data` | `/app/data` | SQLite database storage |
| `./logs` | `/app/logs` | Application logs |
| `./config` | `/app/config` | Configuration files |

## 🔧 Hardware Integration

### Raspberry Pi GPIO

For direct hardware access, uncomment in `docker-compose.yml`:
```yaml
devices:
  - "/dev/spidev0.0:/dev/spidev0.0"
  - "/dev/gpiomem:/dev/gpiomem"
privileged: true
```

### BME280 I2C Connection

| BME280 Pin | Pi Pin | Description |
|------------|--------|-------------|
| VCC | 3.3V | Power |
| GND | GND | Ground |
| SDA | GPIO 2 | I2C Data |
| SCL | GPIO 3 | I2C Clock |

### LoRa Module SPI Connection

| LoRa Pin | Pi Pin | Description |
|----------|--------|-------------|
| VCC | 3.3V | Power |
| GND | GND | Ground |
| MOSI | GPIO 10 | SPI Data Out |
| MISO | GPIO 9 | SPI Data In |
| SCK | GPIO 11 | SPI Clock |
| NSS | GPIO 8 | Chip Select |

## 📊 Monitoring and Logs

### Log Locations

| Deployment | Log Path |
|------------|----------|
| Docker | `docker-compose logs -f` |
| Native Pi | `/var/log/lora-bme280-network/` |
| Development | `./logs/` |

### Health Monitoring

```bash
# Check service health
curl http://localhost:8080/health

# Monitor with watch
watch curl -s http://localhost:8080/health

# Check system resources
docker stats
```

### Log Analysis

```bash
# Follow application logs
tail -f logs/app.log

# Search for errors
grep ERROR logs/app.log

# Filter by sensor ID
grep "node_001" logs/app.log
```

## 🔒 Security

### Firewall Rules

The script automatically configures UFW:
```bash
# SSH access
ufw allow 22/tcp

# Web traffic
ufw allow 80/tcp
ufw allow 443/tcp

# Application port
ufw allow 8080/tcp
```

### SSL Configuration

For production deployments with custom domains:
```bash
# Automatic Let's Encrypt
sudo DOMAIN=yourdomain.com ./deploy.sh cloud production

# Manual certificate renewal
sudo certbot renew
```

### Security Best Practices

1. **Change default passwords** in configuration files
2. **Use environment variables** for sensitive data
3. **Enable firewall** on production systems
4. **Regular security updates** of base images
5. **Monitor access logs** for suspicious activity

## 🛠️ Troubleshooting

### Common Issues

#### Docker Permission Denied
```bash
# Add user to docker group
sudo usermod -aG docker $USER
newgrp docker
```

#### Port Already in Use
```bash
# Find process using port
sudo lsof -i :8080

# Change port
PORT=9000 ./deploy.sh docker production
```

#### I2C Device Not Found
```bash
# Check I2C devices
i2cdetect -y 1

# Enable I2C interface
sudo raspi-config
# Interface Options → I2C → Enable
```

#### Container Won't Start
```bash
# Check container logs
docker-compose logs lora-sensor

# Rebuild containers
docker-compose build --no-cache
docker-compose up -d
```

### System Requirements

#### Minimum Hardware
- **RAM:** 512MB (1GB+ recommended)
- **Storage:** 2GB available space
- **CPU:** ARM Cortex-A7 or equivalent

#### Raspberry Pi Compatibility
- ✅ Raspberry Pi 4 (recommended)
- ✅ Raspberry Pi 3 Model B+
- ✅ Raspberry Pi Zero 2 W
- ⚠️ Raspberry Pi Zero W (limited performance)

### Performance Optimization

#### For Raspberry Pi
```bash
# Increase swap size
sudo dphys-swapfile swapoff
sudo sed -i 's/CONF_SWAPSIZE=100/CONF_SWAPSIZE=1024/' /etc/dphys-swapfile
sudo dphys-swapfile setup
sudo dphys-swapfile swapon
```

#### Docker Memory Limits
```yaml
# In docker-compose.yml
services:
  lora-sensor:
    mem_limit: 256m
    memswap_limit: 512m
```

## 🤝 Contributing

### Development Setup

```bash
# Fork and clone repository
git clone https://github.com/your-username/lora-bme280-network.git
cd lora-bme280-network

# Set up development environment
./deploy.sh development

# Make changes and test
source venv/bin/activate
python app.py
```

### Testing Deployments

```bash
# Test Docker deployment
./deploy.sh docker development

# Test API endpoints
curl http://localhost:8080/health
curl http://localhost:8080/api/sensors
```

### Submitting Changes

1. Create feature branch: `git checkout -b feature-name`
2. Make changes and test thoroughly
3. Update documentation as needed
4. Submit pull request with detailed description

### Code Style

- Follow PEP 8 for Python code
- Use clear, descriptive variable names
- Add comments for complex logic
- Include error handling
- Write tests for new features

## 📞 Support

### Getting Help

1. **Check documentation** - Most issues are covered here
2. **Search issues** - Look for similar problems
3. **Create issue** - Provide detailed information:
   - Operating system and version
   - Deployment target and environment
   - Error messages and logs
   - Steps to reproduce

### Useful Resources

- [Docker Documentation](https://docs.docker.com/)
- [Raspberry Pi Documentation](https://www.raspberrypi.org/documentation/)
- [Flask Documentation](https://flask.palletsprojects.com/)
- [SQLite Documentation](https://www.sqlite.org/docs.html)

## 📄 License

This project is open source and available under the [MIT License](LICENSE).

## 🏷️ Version History

### v1.0.0
- ✅ Universal deployment script
- ✅ Multi-platform Docker support
- ✅ SQLite3 database integration
- ✅ REST API implementation
- ✅ Raspberry Pi optimization
- ✅ Cloud deployment support
- ✅ SSL and security features

---

**Made with ❤️ for the IoT community**

For questions, issues, or contributions, please visit our [GitHub repository](https://github.com/your-repo/lora-bme280-network).