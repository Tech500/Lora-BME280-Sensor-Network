#!/usr/bin/env python3
"""
Digital Ocean LoRa Sensor Data API
Simple Flask server to receive and store BME280 sensor data from your LoRa gateway
Perfect for running alongside RustDesk on your existing droplet
"""

from flask import Flask, request, jsonify, render_template_string, send_file
from flask_cors import CORS
import sqlite3
import json
import os
from datetime import datetime, timedelta
import csv
import io
import logging
from logging.handlers import RotatingFileHandler
import threading
import time

# Initialize Flask app
app = Flask(__name__)
CORS(app)  # Allow cross-origin requests

# Configuration
DATABASE_FILE = '/opt/lora_sensors/sensor_data.db'
LOG_FILE = '/opt/lora_sensors/sensor_api.log'
DATA_RETENTION_DAYS = 90  # Keep 90 days of data
API_KEY = 'your-secure-api-key-here'  # Change this!

# Ensure directories exist
os.makedirs(os.path.dirname(DATABASE_FILE), exist_ok=True)
os.makedirs(os.path.dirname(LOG_FILE), exist_ok=True)

# Setup logging
logging.basicConfig(
    handlers=[RotatingFileHandler(LOG_FILE, maxBytes=10000000, backupCount=5)],
    level=logging.INFO,
    format='%(asctime)s %(levelname)s: %(message)s'
)

def init_database():
    """Initialize SQLite database with sensor data table"""
    conn = sqlite3.connect(DATABASE_FILE)
    cursor = conn.cursor()
    
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS sensor_readings (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            node_id TEXT NOT NULL,
            gateway_timestamp TEXT NOT NULL,
            node_timestamp TEXT NOT NULL,
            temperature_f REAL NOT NULL,
            humidity REAL NOT NULL,
            pressure_hpa REAL NOT NULL,
            heat_index REAL,
            dew_point REAL,
            rssi REAL,
            snr REAL,
            collection_cycle INTEGER,
            gateway_id TEXT,
            received_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        )
    ''')
    
    # Create indexes for better performance
    cursor.execute('CREATE INDEX IF NOT EXISTS idx_node_id ON sensor_readings(node_id)')
    cursor.execute('CREATE INDEX IF NOT EXISTS idx_gateway_timestamp ON sensor_readings(gateway_timestamp)')
    cursor.execute('CREATE INDEX IF NOT EXISTS idx_received_at ON sensor_readings(received_at)')
    
    # Create node status table
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS node_status (
            node_id TEXT PRIMARY KEY,
            last_seen TIMESTAMP,
            total_readings INTEGER DEFAULT 0,
            last_temperature REAL,
            last_humidity REAL,
            last_pressure REAL,
            last_rssi REAL,
            is_active BOOLEAN DEFAULT 1,
            location TEXT
        )
    ''')
    
    conn.commit()
    conn.close()
    logging.info("Database initialized successfully")

def validate_api_key(provided_key):
    """Simple API key validation"""
    return provided_key == API_KEY

@app.route('/api/sensor-data', methods=['POST'])
def receive_sensor_data():
    """
    Receive sensor data from LoRa gateway
    Expected JSON format:
    {
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
    """
    try:
        # Validate API key (optional - remove if you want open access)
        api_key = request.headers.get('X-API-Key', '')
        if API_KEY != 'your-secure-api-key-here' and not validate_api_key(api_key):
            return jsonify({'error': 'Invalid API key'}), 401
        
        data = request.json
        if not data:
            return jsonify({'error': 'No JSON data provided'}), 400
        
        # Validate required fields
        required_fields = ['node_id', 'temperature_f', 'humidity', 'pressure_hpa']
        missing_fields = [field for field in required_fields if field not in data]
        if missing_fields:
            return jsonify({'error': f'Missing required fields: {missing_fields}'}), 400
        
        # Insert into database
        conn = sqlite3.connect(DATABASE_FILE)
        cursor = conn.cursor()
        
        cursor.execute('''
            INSERT INTO sensor_readings 
            (node_id, gateway_timestamp, node_timestamp, temperature_f, humidity, 
             pressure_hpa, heat_index, dew_point, rssi, snr, collection_cycle, gateway_id)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ''', (
            data.get('node_id'),
            data.get('gateway_timestamp', ''),
            data.get('node_timestamp', ''),
            data.get('temperature_f'),
            data.get('humidity'),
            data.get('pressure_hpa'),
            data.get('heat_index'),
            data.get('dew_point'),
            data.get('rssi'),
            data.get('snr'),
            data.get('collection_cycle'),
            data.get('gateway_id', 'UNKNOWN')
        ))
        
        # Update node status
        cursor.execute('''
            INSERT OR REPLACE INTO node_status 
            (node_id, last_seen, total_readings, last_temperature, last_humidity, 
             last_pressure, last_rssi, is_active)
            VALUES (?, CURRENT_TIMESTAMP, 
                    COALESCE((SELECT total_readings FROM node_status WHERE node_id = ?) + 1, 1),
                    ?, ?, ?, ?, 1)
        ''', (
            data.get('node_id'),
            data.get('node_id'),
            data.get('temperature_f'),
            data.get('humidity'),
            data.get('pressure_hpa'),
            data.get('rssi')
        ))
        
        conn.commit()
        conn.close()
        
        logging.info(f"Received data from {data.get('node_id')}: {data.get('temperature_f')}°F, {data.get('humidity')}%")
        
        return jsonify({
            'status': 'success',
            'message': 'Data stored successfully',
            'node_id': data.get('node_id'),
            'timestamp': datetime.now().isoformat()
        }), 200
        
    except Exception as e:
        logging.error(f"Error processing sensor data: {e}")
        return jsonify({'error': str(e)}), 500

@app.route('/', methods=['GET'])
def dashboard():
    """Simple web dashboard to view sensor data"""
    try:
        conn = sqlite3.connect(DATABASE_FILE)
        cursor = conn.cursor()
        
        # Get recent readings (last 24 hours)
        cursor.execute('''
            SELECT node_id, gateway_timestamp, temperature_f, humidity, pressure_hpa, 
                   rssi, received_at
            FROM sensor_readings 
            WHERE received_at > datetime('now', '-24 hours')
            ORDER BY received_at DESC 
            LIMIT 100
        ''')
        recent_readings = cursor.fetchall()
        
        # Get node status
        cursor.execute('''
            SELECT node_id, last_seen, total_readings, last_temperature, 
                   last_humidity, last_pressure, last_rssi, is_active
            FROM node_status
            ORDER BY last_seen DESC
        ''')
        node_status = cursor.fetchall()
        
        conn.close()
        
        # HTML template
        html_template = '''
        <!DOCTYPE html>
        <html>
        <head>
            <title>LoRa Sensor Dashboard</title>
            <meta http-equiv="refresh" content="60">
            <style>
                body { font-family: Arial, sans-serif; margin: 20px; }
                .header { background: #007acc; color: white; padding: 15px; border-radius: 5px; }
                .section { margin: 20px 0; }
                table { border-collapse: collapse; width: 100%; }
                th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }
                th { background-color: #f2f2f2; }
                .active { color: green; font-weight: bold; }
                .inactive { color: red; }
                .temp-high { background-color: #ffebee; }
                .humidity-high { background-color: #e3f2fd; }
            </style>
        </head>
        <body>
            <div class="header">
                <h1>🌡️ LoRa Sensor Network Dashboard</h1>
                <p>Digital Ocean • Last updated: {{ current_time }}</p>
            </div>
            
            <div class="section">
                <h2>📊 Node Status</h2>
                <table>
                    <tr>
                        <th>Node ID</th>
                        <th>Status</th>
                        <th>Last Seen</th>
                        <th>Total Readings</th>
                        <th>Last Temperature</th>
                        <th>Last Humidity</th>
                        <th>Last Pressure</th>
                        <th>Signal (RSSI)</th>
                    </tr>
                    {% for node in node_status %}
                    <tr>
                        <td><strong>{{ node[0] }}</strong></td>
                        <td class="{{ 'active' if node[7] else 'inactive' }}">
                            {{ 'Active' if node[7] else 'Inactive' }}
                        </td>
                        <td>{{ node[1] }}</td>
                        <td>{{ node[2] }}</td>
                        <td class="{{ 'temp-high' if node[3] and node[3] > 80 else '' }}">
                            {{ '%.1f°F' % node[3] if node[3] else 'N/A' }}
                        </td>
                        <td class="{{ 'humidity-high' if node[4] and node[4] > 70 else '' }}">
                            {{ '%.1f%%' % node[4] if node[4] else 'N/A' }}
                        </td>
                        <td>{{ '%.2f hPa' % node[5] if node[5] else 'N/A' }}</td>
                        <td>{{ '%.1f dBm' % node[6] if node[6] else 'N/A' }}</td>
                    </tr>
                    {% endfor %}
                </table>
            </div>
            
            <div class="section">
                <h2>📈 Recent Readings (Last 24 Hours)</h2>
                <p><a href="/api/export/csv">Download CSV</a> | <a href="/api/export/json">Download JSON</a></p>
                <table>
                    <tr>
                        <th>Node</th>
                        <th>Gateway Time</th>
                        <th>Temperature</th>
                        <th>Humidity</th>
                        <th>Pressure</th>
                        <th>Signal</th>
                        <th>Received</th>
                    </tr>
                    {% for reading in recent_readings %}
                    <tr>
                        <td><strong>{{ reading[0] }}</strong></td>
                        <td>{{ reading[1] }}</td>
                        <td class="{{ 'temp-high' if reading[2] > 80 else '' }}">{{ '%.1f°F' % reading[2] }}</td>
                        <td class="{{ 'humidity-high' if reading[3] > 70 else '' }}">{{ '%.1f%%' % reading[3] }}</td>
                        <td>{{ '%.2f hPa' % reading[4] }}</td>
                        <td>{{ '%.1f dBm' % reading[5] if reading[5] else 'N/A' }}</td>
                        <td>{{ reading[6] }}</td>
                    </tr>
                    {% endfor %}
                </table>
            </div>
            
            <div class="section">
                <h2>🔗 API Endpoints</h2>
                <ul>
                    <li><code>POST /api/sensor-data</code> - Receive sensor data</li>
                    <li><code>GET /api/nodes</code> - Get all nodes JSON</li>
                    <li><code>GET /api/readings?node=NODE01&hours=24</code> - Get specific readings</li>
                    <li><code>GET /api/export/csv</code> - Export all data as CSV</li>
                </ul>
            </div>
        </body>
        </html>
        '''
        
        from jinja2 import Template
        template = Template(html_template)
        
        return template.render(
            current_time=datetime.now().strftime('%Y-%m-%d %H:%M:%S'),
            node_status=node_status,
            recent_readings=recent_readings
        )
        
    except Exception as e:
        logging.error(f"Dashboard error: {e}")
        return f"Dashboard error: {e}", 500

@app.route('/api/nodes', methods=['GET'])
def get_nodes():
    """Get all nodes as JSON"""
    try:
        conn = sqlite3.connect(DATABASE_FILE)
        cursor = conn.cursor()
        
        cursor.execute('''
            SELECT node_id, last_seen, total_readings, last_temperature,
                   last_humidity, last_pressure, last_rssi, is_active
            FROM node_status
            ORDER BY last_seen DESC
        ''')
        
        nodes = []
        for row in cursor.fetchall():
            nodes.append({
                'node_id': row[0],
                'last_seen': row[1],
                'total_readings': row[2],
                'last_temperature': row[3],
                'last_humidity': row[4],
                'last_pressure': row[5],
                'last_rssi': row[6],
                'is_active': bool(row[7])
            })
        
        conn.close()
        return jsonify(nodes)
        
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/readings', methods=['GET'])
def get_readings():
    """Get sensor readings with optional filtering"""
    try:
        node_id = request.args.get('node')
        hours = int(request.args.get('hours', 24))
        limit = int(request.args.get('limit', 1000))
        
        conn = sqlite3.connect(DATABASE_FILE)
        cursor = conn.cursor()
        
        query = '''
            SELECT node_id, gateway_timestamp, node_timestamp, temperature_f, 
                   humidity, pressure_hpa, heat_index, dew_point, rssi, snr,
                   collection_cycle, received_at
            FROM sensor_readings
            WHERE received_at > datetime('now', '-%d hours')
        ''' % hours
        
        params = []
        if node_id:
            query += ' AND node_id = ?'
            params.append(node_id)
        
        query += ' ORDER BY received_at DESC LIMIT ?'
        params.append(limit)
        
        cursor.execute(query, params)
        
        readings = []
        for row in cursor.fetchall():
            readings.append({
                'node_id': row[0],
                'gateway_timestamp': row[1],
                'node_timestamp': row[2],
                'temperature_f': row[3],
                'humidity': row[4],
                'pressure_hpa': row[5],
                'heat_index': row[6],
                'dew_point': row[7],
                'rssi': row[8],
                'snr': row[9],
                'collection_cycle': row[10],
                'received_at': row[11]
            })
        
        conn.close()
        return jsonify(readings)
        
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/export/csv', methods=['GET'])
def export_csv():
    """Export all data as CSV file"""
    try:
        days = int(request.args.get('days', 7))
        
        conn = sqlite3.connect(DATABASE_FILE)
        cursor = conn.cursor()
        
        cursor.execute('''
            SELECT node_id, gateway_timestamp, node_timestamp, temperature_f,
                   humidity, pressure_hpa, heat_index, dew_point, rssi, snr,
                   collection_cycle, gateway_id, received_at
            FROM sensor_readings
            WHERE received_at > datetime('now', '-%d days')
            ORDER BY received_at DESC
        ''' % days)
        
        # Create CSV in memory
        output = io.StringIO()
        writer = csv.writer(output)
        
        # Write header
        writer.writerow([
            'node_id', 'gateway_timestamp', 'node_timestamp', 'temperature_f',
            'humidity', 'pressure_hpa', 'heat_index', 'dew_point', 'rssi', 'snr',
            'collection_cycle', 'gateway_id', 'received_at'
        ])
        
        # Write data
        for row in cursor.fetchall():
            writer.writerow(row)
        
        conn.close()
        
        # Convert to bytes for download
        output.seek(0)
        csv_data = output.getvalue().encode('utf-8')
        
        return send_file(
            io.BytesIO(csv_data),
            mimetype='text/csv',
            as_attachment=True,
            download_name=f'lora_sensor_data_{datetime.now().strftime("%Y%m%d")}.csv'
        )
        
    except Exception as e:
        return jsonify({'error': str(e)}), 500

def cleanup_old_data():
    """Cleanup thread to remove old data"""
    while True:
        try:
            conn = sqlite3.connect(DATABASE_FILE)
            cursor = conn.cursor()
            
            # Delete readings older than retention period
            cursor.execute(
                'DELETE FROM sensor_readings WHERE received_at < datetime("now", "-%d days")' % DATA_RETENTION_DAYS
            )
            deleted_count = cursor.rowcount
            
            conn.commit()
            conn.close()
            
            if deleted_count > 0:
                logging.info(f"Cleaned up {deleted_count} old records")
            
        except Exception as e:
            logging.error(f"Cleanup error: {e}")
        
        # Sleep for 24 hours
        time.sleep(86400)

if __name__ == '__main__':
    # Initialize database
    init_database()
    
    # Start cleanup thread
    cleanup_thread = threading.Thread(target=cleanup_old_data, daemon=True)
    cleanup_thread.start()
    
    # Start Flask app
    logging.info("Starting LoRa Sensor API server")
    app.run(host='0.0.0.0', port=5000, debug=False)