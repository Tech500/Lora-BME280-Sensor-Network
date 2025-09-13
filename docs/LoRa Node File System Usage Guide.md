# LoRa Node File System Usage Guide

## 🎯 **System Overview**

Your enhanced LoRa gateway now provides:

- **📁 Individual CSV files** for each sensor node (e.g., `/node_NODE01_2025-09-03.csv`)
- **📤 Automatic web requests** sent when new sensor data arrives
- **🌐 REST API** for downloading and managing node data
- **🔄 File rotation** and cleanup to prevent storage overflow
- **📊 Detailed statistics** and monitoring per node

---

## 📊 **File Structure Examples**

### Individual Node File Format:
```csv
record_id,gateway_timestamp,node_timestamp,temperature_f,humidity,pressure_hpa,heat_index,dew_point,rssi,snr,collection_cycle
NODE01_1,Wed-09-03-2025--14:00:15,2025-09-03-14:00:10,75.2,65.1,1013.25,78.5,12.3,-85.2,9.5,56
NODE01_2,Wed-09-03-2025--14:15:18,2025-09-03-14:15:12,76.1,64.8,1013.20,79.2,12.1,-84.8,9.8,57
NODE01_3,Wed-09-03-2025--14:30:21,2025-09-03-14:30:15,77.0,63.9,1013.18,80.1,11.9,-85.1,9.2,58
```

### File Naming Convention:
- **Daily files**: `/node_NODE01_2025-09-03.csv`
- **Automatic rotation**: New file each day or after 1000 records
- **Retention**: Files kept for 7 days (configurable)

---

## 🌐 **Web API Endpoints**

### **Main Dashboard**
```
GET /
```
Shows gateway status, all nodes, and recent data in HTML format.

### **Download Node Data**
```
GET /download?node=NODE01
GET /download?node=NODE01&date=2025-09-03
```
Downloads CSV file for specific node and date.

**Example URLs:**
- `http://192.168.1.100/download?node=NODE01` - Today's data for NODE01
- `http://192.168.1.100/download?node=NODE02&date=2025-09-02` - Specific date

### **Node Statistics (JSON API)**
```
GET /node-stats
```

**Example Response:**
```json
[
  {
    "nodeId": "NODE01",
    "currentFile": "/node_NODE01_2025-09-03.csv",
    "recordCount": 47,
    "totalRecords": 1543,
    "isActive": true,
    "lastSeenMinutes": 12,
    "lastRSSI": -85.2,
    "hasPendingWebRequest": false
  },
  {
    "nodeId": "NODE02",
    "currentFile": "/node_NODE02_2025-09-03.csv",
    "recordCount": 45,
    "totalRecords": 1398,
    "isActive": true,
    "lastSeenMinutes": 14,
    "lastRSSI": -87.1,
    "hasPendingWebRequest": true
  }
]
```

### **Manual Collection**
```
GET /manual
```
Triggers immediate data collection from all nodes.

---

## 📤 **Automatic Web Requests**

When sensor data is received, the gateway automatically sends HTTP POST requests to your configured endpoint.

### **Configuration**
```cpp
const String WEB_REQUEST_ENDPOINT = "http://your-server.com/api/sensor-data";
```

### **Auto-Request Payload**
```json
{
  "record_id": "NODE01_1547",
  "node_id": "NODE01",
  "gateway_timestamp": "Wed-09-03-2025--14:30:25",
  "node_timestamp": "2025-09-03-14:30:20",
  "temperature_f": 75.2,
  "humidity": 65.1,
  "pressure_hpa": 1013.25,
  "heat_index": 78.5,
  "dew_point": 12.3,
  "rssi": -85.2,
  "snr": 9.5,
  "collection_cycle": 58,
  "gateway_id": "GATEWAY_01"
}
```

### **HTTP Headers Sent**
```
Content-Type: application/json
X-Gateway-ID: LoRa-Gateway-01
X-Node-ID: NODE01
```

---

## 🔧 **Configuration Options**

### **Enable/Disable Features**
```cpp
#define ENABLE_NODE_FILES true              // Individual node files
#define ENABLE_AUTO_WEB_REQUESTS true       // Automatic HTTP requests
```

### **File Management**
```cpp
const unsigned long FILE_RETENTION_HOURS = 168;  // 7 days
const int MAX_RECORDS_PER_FILE = 1000;          // Records before rotation
```

### **Web Endpoint**
```cpp
const String WEB_REQUEST_ENDPOINT = "https://your-api.com/sensor-data";
```

---

## 📊 **Use Cases & Benefits**

### **🔍 Data Analysis**
- **Per-node analysis**: Each CSV file contains all data for one specific sensor
- **Historical trends**: Easy to plot temperature/humidity trends for each location
- **Correlation analysis**: Compare different sensor locations using separate files

### **🌐 Real-time Integration**
- **Instant notifications**: Auto web requests notify your server immediately
- **Database insertion**: Direct integration with your database/API
- **Alert systems**: Trigger alerts based on specific sensor readings

### **📈 Monitoring & Maintenance**
- **Node health tracking**: Monitor RSSI, success rates, and response times
- **Predictive maintenance**: Identify nodes with declining performance
- **Network optimization**: Analyze which nodes need attention

---

## 💡 **Example Integration Scripts**

### **Python Server to Receive Auto Requests**
```python
from flask import Flask, request, jsonify
import sqlite3
import json

app = Flask(__name__)

@app.route('/api/sensor-data', methods=['POST'])
def receive_sensor_data():
    try:
        data = request.json
        
        # Validate required fields
        required_fields = ['node_id', 'temperature_f', 'humidity', 'pressure_hpa']
        if not all(field in data for field in required_fields):
            return jsonify({'error': 'Missing required fields'}), 400
        
        # Insert into database
        conn = sqlite3.connect('sensor_data.db')
        cursor = conn.cursor()
        
        cursor.execute('''
            INSERT INTO sensor_readings 
            (node_id, gateway_timestamp, temperature, humidity, pressure, rssi, snr)
            VALUES (?, ?, ?, ?, ?, ?, ?)
        ''', (
            data['node_id'],
            data['gateway_timestamp'],
            data['temperature_f'],
            data['humidity'],
            data['pressure_hpa'],
            data['rssi'],
            data['snr']
        ))
        
        conn.commit()
        conn.close()
        
        print(f"✅ Received data from {data['node_id']}: {data['temperature_f']}°F")
        
        return jsonify({'status': 'success', 'message': 'Data stored'}), 200
        
    except Exception as e:
        print(f"❌ Error processing sensor data: {e}")
        return jsonify({'error': str(e)}), 500

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)
```

### **Node.js Express Server**
```javascript
const express = require('express');
const fs = require('fs');
const path = require('path');

const app = express();
app.use(express.json());

app.post('/api/sensor-data', (req, res) => {
    try {
        const data = req.body;
        
        // Log to file (one file per node)
        const filename = `sensor_logs/${data.node_id}_${new Date().toISOString().split('T')[0]}.json`;
        
        // Append data to daily log file
        fs.appendFileSync(filename, JSON.stringify(data) + '\n');
        
        console.log(`✅ Data from ${data.node_id}: ${data.temperature_f}°F`);
        
        res.json({ status: 'success', message: 'Data logged' });
        
    } catch (error) {
        console.error('❌ Error:', error);
        res.status(500).json({ error: error.message });
    }
});

app.listen(3000, () => {
    console.log('🚀 Sensor data server running on port 3000');
});
```

---

## 🚀 **Getting Started**

### **1. Update Configuration**
```cpp
// Set your web endpoint
const String WEB_REQUEST_ENDPOINT = "http://192.168.1.200:5000/api/sensor-data";

// Add your node IDs
NodeFileManager nodeManagers[] = {
  {"NODE01", "", 0, 0, true, 0, 0, 0, 0, {}, false, ""},
  {"NODE02", "", 0, 0, true, 0, 0, 0, 0, {}, false, ""},
  {"NODE03", "", 0, 0, true, 0, 0, 0, 0, {}, false, ""},
  // Add more nodes as needed
};
```

### **2. Deploy & Monitor**
1. **Flash** the updated code to your gateway
2. **Access** the web dashboard at `http://[gateway-ip]/`
3. **Monitor** individual node files and statistics
4. **Set up** your receiving server for auto web requests
5. **Download** CSV files for analysis

### **3. Data Flow**
```
Sensor Node → LoRa → Gateway → Individual CSV File → Auto Web Request → Your Server
```

---

## 🎉 **Benefits Summary**

✅ **Individual tracking** - Each sensor gets its own organized data file  
✅ **Real-time integration** - Instant web requests to your systems  
✅ **Easy analysis** - CSV format perfect for Excel, Python, R  
✅ **Automatic maintenance** - File rotation and cleanup  
✅ **Network monitoring** - Track node health and performance  
✅ **Scalable** - Easy to add/remove nodes  
✅ **Reliable** - Local storage + web requests for redundancy  

Your 20-22µA sensor nodes now have enterprise-level data management! 🚀