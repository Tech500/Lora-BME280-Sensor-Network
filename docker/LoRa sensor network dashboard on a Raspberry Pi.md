# LoRa Sensor Network - Raspberry Pi Installation Guide

Complete step-by-step installation guide for setting up the LoRa sensor network dashboard on a Raspberry Pi.

## Prerequisites

- Raspberry Pi 4 (2GB+ RAM recommended)
- MicroSD card (32GB+ recommended)
- Stable power supply
- Network connection (WiFi or Ethernet)
- Computer for initial setup

## Step 1: Prepare Raspberry Pi OS

### Download and Flash OS
1. Download [Raspberry Pi Imager](https://www.raspberrypi.com/software/)
2. Flash **Raspberry Pi OS (64-bit)** to your SD card
3. Enable SSH and configure WiFi using the imager's advanced options

### Initial Setup
1. Insert SD card and boot the Pi
2. Complete the initial setup wizard
3. Update the system:
```bash
sudo apt update && sudo apt upgrade -y
```

## Step 2: Install Docker

### Install Docker Engine
```bash
# Download and run Docker installation script
curl -fsSL https://get.docker.com -o get-docker.sh
sudo sh get-docker.sh

# Add your user to docker group
sudo usermod -aG docker $USER

# Clean up
rm get-docker.sh
```

### Verify Installation
```bash
# Reboot to apply group changes
sudo reboot

# After reboot, test Docker
docker --version
docker run hello-world
```

## Step 3: Install Project Files

### Option A: Clone from GitHub
```bash
# Install git if not present
sudo apt install git -y

# Clone the repository
git clone https://github.com/tech500/lora-bme280-sensor-network.git
cd lora-bme280-sensor-network/server
```

### Option B: Manual File Transfer
If you have the project files locally:
```bash
# Create project directory
mkdir -p ~/lora-sensor-network/server
cd ~/lora-sensor-network/server

# Copy files via SCP from your computer:
# scp -r /path/to/project/* pi@raspberry-pi-ip:~/lora-sensor-network/server/
```

## Step 4: Build and Deploy

### Build Docker Container
```bash
# Navigate to server directory
cd ~/lora-bme280-sensor-network/server

# Build the container
docker build -t lora-sensor-dashboard .
```

### Create Data Directory
```bash
# Ensure data directory exists with proper permissions
mkdir -p ./data
chmod 755 ./data
```

### Run the Container
```bash
# Run the dashboard container
docker run -d \
  --name lora-sensors \
  -p 5001:5001 \
  -v $(pwd)/data:/app/data \
  --restart unless-stopped \
  lora-sensor-dashboard
```

## Step 5: Verify Installation

### Check Container Status
```bash
# Verify container is running
docker ps

# Check logs for any errors
docker logs lora-sensors
```

### Test Web Interface
1. Find your Pi's IP address:
```bash
hostname -I
```

2. Open web browser and navigate to:
```
http://your-pi-ip:5001
```

3. You should see the LoRa sensor dashboard

## Step 6: Configure Network Access

### Enable Port Access
If using UFW firewall:
```bash
sudo ufw allow 5001/tcp
```

### Static IP (Recommended)
Edit network configuration:
```bash
sudo nano /etc/dhcpcd.conf
```

Add at the end:
```
interface wlan0
static ip_address=192.168.1.100/24
static routers=192.168.1.1
static domain_name_servers=192.168.1.1 8.8.8.8
```

Restart networking:
```bash
sudo systemctl restart dhcpcd
```

## Step 7: Configure Auto-Start

### Enable Docker Service
```bash
sudo systemctl enable docker
```

### Verify Auto-Start
Reboot and check:
```bash
sudo reboot

# After reboot
docker ps
```

## Maintenance Commands

### View Logs
```bash
# Real-time logs
docker logs -f lora-sensors

# Last 50 lines
docker logs --tail 50 lora-sensors
```

### Restart Container
```bash
docker restart lora-sensors
```

### Update Application
```bash
# Stop and remove old container
docker stop lora-sensors
docker rm lora-sensors

# Pull latest code (if using git)
git pull

# Rebuild and restart
docker build -t lora-sensor-dashboard .
docker run -d \
  --name lora-sensors \
  -p 5001:5001 \
  -v $(pwd)/data:/app/data \
  --restart unless-stopped \
  lora-sensor-dashboard
```

### Backup Data
```bash
# Backup database
cp ./data/lora_sensors.db ./data/lora_sensors_backup_$(date +%Y%m%d).db

# Or copy to another location
scp ./data/lora_sensors.db user@backup-server:/backup/location/
```

## Troubleshooting

### Container Won't Start
```bash
# Check Docker service
sudo systemctl status docker

# Check disk space
df -h

# Check container logs
docker logs lora-sensors
```

### Port Already in Use
```bash
# Find what's using port 5001
sudo netstat -tulpn | grep 5001

# Kill the process or change port in docker run command
docker run -d --name lora-sensors -p 5002:5001 ...
```

### Permission Issues
```bash
# Fix data directory permissions
sudo chown -R $USER:$USER ./data
chmod -R 755 ./data
```

### Memory Issues
```bash
# Check memory usage
free -h

# If low on memory, increase swap
sudo dphys-swapfile swapoff
sudo nano /etc/dphys-swapfile
# Change CONF_SWAPSIZE=1024
sudo dphys-swapfile setup
sudo dphys-swapfile swapon
```

## Performance Optimization

### SD Card Health
```bash
# Check SD card health
sudo dmesg | grep mmc

# Consider using USB 3.0 SSD for better performance
```

### Docker Cleanup
```bash
# Remove unused containers and images
docker system prune -f

# Remove old images
docker image prune -f
```

## Security Considerations

### Basic Security
```bash
# Change default password
passwd

# Update system regularly
sudo apt update && sudo apt upgrade -y

# Consider fail2ban for SSH protection
sudo apt install fail2ban -y
```

### Firewall Setup
```bash
# Enable UFW
sudo ufw enable

# Allow SSH
sudo ufw allow ssh

# Allow dashboard port
sudo ufw allow 5001/tcp

# Check status
sudo ufw status
```

## Hardware Recommendations

### Recommended Specs
- **Raspberry Pi 4 Model B** (4GB RAM preferred)
- **Samsung EVO Select 32GB+ microSD** (Class 10, U3)
- **Official Raspberry Pi Power Supply** (USB-C, 5.1V/3A)
- **Case with cooling** (heatsinks or fan recommended)

### Optional Upgrades
- **USB 3.0 SSD** for database storage (better performance/reliability)
- **PoE HAT** for power over ethernet
- **Real-time clock module** if internet connection is unreliable

## Next Steps

After successful installation:

1. **Configure your LoRa gateway** with this Pi's IP address
2. **Set up sensor nodes** to transmit to the gateway
3. **Monitor the dashboard** for incoming data
4. **Set up backup procedures** for your sensor data

## Support

If you encounter issues:

1. Check the troubleshooting section above
2. Review Docker and application logs
3. Verify network connectivity
4. Check hardware connections
5. Open an issue on the project GitHub repository

Remember to include relevant log output and system information when seeking help.