# LoRa Sensor Network - Home Assistant Integration Guide

## Overview
This guide helps you integrate your LoRa Environmental Sensor Network with Home Assistant using a custom Lovelace card. The card displays real-time data from all your LoRa sensor nodes in a clean, organized interface.

## Prerequisites

### Hardware Requirements
- LoRa Gateway with BME280 sensors deployed
- Gateway running on your local network
- Home Assistant installation (Core, Supervised, or OS)

### Software Requirements  
- Home Assistant 2023.1 or newer
- Access to Home Assistant configuration files
- Network connectivity between Home Assistant and your LoRa Gateway

## Installation Steps

### Step 1: Download the Custom Card

1. Download the `lora-sensor-card.js` file from the releases page
2. Copy it to your Home Assistant `www` directory:
   ```
   /config/www/lora-sensor-card.js
   ```

### Step 2: Register the Custom Card

1. Open Home Assistant
2. Go to **Configuration** → **Lovelace Dashboards** → **Resources**
3. Click **Add Resource**
4. Enter the following:
   - **URL**: `/local/lora-sensor-card.js`
   - **Resource type**: JavaScript Module
5. Click **Create**

### Step 3: Find Your Gateway IP Address

You need to identify the IP address of your LoRa Gateway:

**Method 1: Router Admin Panel**
1. Log into your router's admin interface
2. Look for "Connected Devices" or "DHCP Clients"
3. Find your LoRa Gateway device

**Method 2: Network Scanner**
1. Use a network scanning app on your phone
2. Look for devices on port 5001
3. Test by opening `http://[IP]:5001/api/sensor-data/latest` in a browser

**Method 3: Command Line** (Linux/Mac)
```bash
nmap -p 5001 192.168.1.0/24
```

### Step 4: Configure the Card

1. Edit your Home Assistant dashboard
2. Click **Add Card** → **Manual**
3. Copy and paste the YAML template (provided separately)
4. Update the `gateway_url` with your Gateway IP address:
   ```yaml
   gateway_url: "http://192.168.1.XXX:5001"  # Replace XXX with your IP
   ```

## Configuration Options

### Basic Settings
- **gateway_url**: Your LoRa Gateway IP address and port
- **refresh_interval**: How often to update data (30-300 seconds)
- **temperature_unit**: Display temperature in Fahrenheit (F) or Celsius (C)

### Display Options
- **compact_mode**: Use compact layout for many nodes (10+)
- **nodes_per_row**: Number of sensor tiles per row
- **show_battery_levels**: Display battery status indicators
- **show_signal_strength**: Show RSSI values and signal strength

### Status Thresholds
- **offline_threshold**: Seconds before marking a node offline (default: 300)
- **low_battery_threshold**: Voltage below this shows warning (default: 3.0V)
- **weak_signal_threshold**: RSSI below this shows warning (default: -90 dBm)

### Node Customization
You can assign friendly names to your sensor nodes:
```yaml
node_names:
  "1001": "Living Room"
  "1002": "Master Bedroom"
  "1003": "Kitchen"
```

### Room Grouping
Organize nodes by location:
```yaml
room_groups:
  "Upstairs":
    - "1001"
    - "1002"
  "Downstairs":
    - "1003"
    - "1004"
```

## Example Configurations

### Small Installation (2-4 nodes)
```yaml
type: custom:lora-sensor-card
title: "Home Environmental Sensors"
gateway_url: "http://192.168.1.150:5001"
settings:
  refresh_interval: 30
  temperature_unit: "F"
  compact_mode: false
  nodes_per_row: 2
```

### Large Installation (10+ nodes)
```yaml
type: custom:lora-sensor-card
title: "LoRa Sensor Network"
gateway_url: "http://192.168.1.150:5001"
settings:
  refresh_interval: 60
  compact_mode: true
  nodes_per_row: 4
  show_timestamps: false
```

## Troubleshooting

### Card Not Appearing
1. Check that the resource is properly registered in Home Assistant
2. Clear browser cache (Ctrl+F5)
3. Verify the JavaScript file is in `/config/www/`
4. Check Home Assistant logs for JavaScript errors

### No Data Displayed
1. Test gateway connectivity:
   ```
   http://YOUR_GATEWAY_IP:5001/api/sensor-data/latest
   ```
2. Verify the gateway URL in your configuration
3. Check firewall settings on both devices
4. Ensure LoRa Gateway is running and accessible

### Connection Errors
1. **Timeout errors**: Increase `api_timeout` in advanced settings
2. **Network unreachable**: Check IP address and port
3. **Mixed content warnings**: Ensure both Home Assistant and Gateway use HTTP or HTTPS

### Performance Issues
1. Increase `refresh_interval` for many nodes
2. Enable `compact_mode` to reduce visual complexity
3. Hide unused nodes with `hidden_nodes` configuration
4. Consider grouping nodes by room for better organization

## Network Security

### Firewall Configuration
Ensure port 5001 is accessible between Home Assistant and your Gateway:
```bash
# Allow traffic on port 5001
sudo ufw allow 5001
```

### HTTPS Considerations
If your Home Assistant uses HTTPS, you may need to:
1. Configure SSL on your LoRa Gateway, or
2. Add your Gateway to Home Assistant's `allowlist_external_urls`

## API Reference

The card uses these endpoints from your LoRa Gateway:
- `GET /api/sensor-data/latest` - Current readings from all nodes
- `GET /api/sensor-data/history` - Historical data (if available)

Expected JSON response format:
```json
{
  "success": true,
  "data": {
    "1001": {
      "node_id": "1001",
      "temperature": 22.5,
      "humidity": 45.2,
      "pressure": 1013.2,
      "battery_voltage": 3.7,
      "rssi": -75,
      "timestamp": {
        "formatted": "2025-01-15 14:30:25"
      }
    }
  }
}
```

## Support and Updates

### Getting Help
1. Check the troubleshooting section above
2. Verify your gateway is accessible from Home Assistant
3. Check Home Assistant logs for specific error messages

### Updating the Card
1. Download the latest version of `lora-sensor-card.js`
2. Replace the file in `/config/www/`
3. Hard refresh your browser (Ctrl+F5)
4. Update your YAML configuration if needed

## Advanced Usage

### Custom Styling
You can add custom CSS to match your Home Assistant theme:
```yaml
advanced:
  custom_css: |
    .lora-card {
      background: var(--card-background-color);
      border-radius: 12px;
    }
```

### Multiple Gateways
If you have multiple LoRa Gateways, create separate cards:
```yaml
# First gateway
type: custom:lora-sensor-card
title: "Indoor Sensors"
gateway_url: "http://192.168.1.150:5001"

# Second gateway  
type: custom:lora-sensor-card
title: "Outdoor Sensors"
gateway_url: "http://192.168.1.151:5001"
```

## Standalone Dashboard Integration

The card complements your existing LoRa sensor dashboard:
- **Card**: Quick overview and status in Home Assistant
- **Dashboard**: Detailed analysis, charts, and configuration

You can link to your full dashboard from the card by adding:
```yaml
settings:
  dashboard_url: "http://192.168.1.150:5001"
  show_dashboard_link: true
```

---

**Note**: This card is designed to work with the EoRa Pi LoRa Sensor Network system. For detailed sensor monitoring and historical analysis, continue using your specialized dashboard interface.