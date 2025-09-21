# app.py - Complete Flask API with Timezone Support
from flask import Flask, request, jsonify, render_template_string, render_templa                                                                                                             te, session, send_from_directory
from datetime import datetime
import pytz
import sqlite3
import json
import os
from functools import wraps
import sys
import logging

app = Flask(__name__)
app.secret_key = os.environ.get('SECRET_KEY', 'your-secret-key-here')

# Configuration
DATABASE_PATH = os.environ.get('DATABASE_PATH', '/app/data/lora_sensors.db')
CONFIG_PATH = os.environ.get('CONFIG_PATH', '/app/config/settings.json')

# Default settings
DEFAULT_SETTINGS = {
    "timezone": "UTC",
    "refresh_interval": 30,
    "dashboard": {
        "title": "LoRa Sensor Network",
        "theme": "default"
    }
}

def load_settings():
    """Load settings from config file"""
    try:
        if os.path.exists(CONFIG_PATH):
            with open(CONFIG_PATH, 'r') as f:
                return json.load(f)
    except Exception as e:
        print(f"Error loading settings: {e}")
    return DEFAULT_SETTINGS

def save_settings(settings):
    """Save settings to config file"""
    try:
        os.makedirs(os.path.dirname(CONFIG_PATH), exist_ok=True)
        with open(CONFIG_PATH, 'w') as f:
            json.dump(settings, f, indent=2)
        return True
    except Exception as e:
        print(f"Error saving settings: {e}")
        return False

def get_user_timezone():
    """Get user's timezone preference"""
    # Check session first, then config file
    user_tz = session.get('timezone')
    if not user_tz:
        settings = load_settings()
        user_tz = settings.get('timezone', 'UTC')
    return user_tz

def format_timestamp_for_user(utc_timestamp, timezone_str=None):
    """Convert UTC timestamp to user's timezone"""
    if timezone_str is None:
        timezone_str = get_user_timezone()

    try:
        # Parse UTC timestamp
        if isinstance(utc_timestamp, str):
            dt = datetime.fromisoformat(utc_timestamp.replace('Z', '+00:00'))
        else:
            dt = utc_timestamp

        # Convert to UTC if not already
        if dt.tzinfo is None:
            dt = pytz.UTC.localize(dt)

        # Convert to user timezone
        user_tz = pytz.timezone(timezone_str)
        local_dt = dt.astimezone(user_tz)

        return {
            'utc': dt.isoformat(),
            'local': local_dt.isoformat(),
            'formatted': local_dt.strftime('%Y-%m-%d %H:%M:%S %Z'),
            'timezone': timezone_str
        }
    except Exception as e:
        print(f"Error formatting timestamp: {e}")
        return {
            'utc': str(utc_timestamp),
            'local': str(utc_timestamp),
            'formatted': str(utc_timestamp),
            'timezone': 'UTC'
        }

# Database initialization
def init_database():
    """Initialize SQLite database"""
    try:
        os.makedirs(os.path.dirname(DATABASE_PATH), exist_ok=True)
        conn = sqlite3.connect(DATABASE_PATH)
        cursor = conn.cursor()

        # Create sensor_data table
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS sensor_data (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                node_id TEXT NOT NULL,
                temperature REAL,
                humidity REAL,
                pressure REAL,
                battery_voltage REAL,
                rssi INTEGER,
                snr REAL,
                timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
                created_at DATETIME DEFAULT CURRENT_TIMESTAMP
            )
        ''')

        # Create settings table
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS settings (
                key TEXT PRIMARY KEY,
                value TEXT,
                updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
            )
        ''')

        conn.commit()
        conn.close()
        print("Database initialized successfully")
    except Exception as e:
        print(f"Database initialization error: {e}")

# API Routes

@app.route('/static/<path:filename>')
def serve_static_files(filename):
    return send_from_directory('static', filename)

@app.route('/api/settings', methods=['GET'])
def get_settings():
    """Get current settings"""
    try:
        settings = load_settings()
        return jsonify({
            'success': True,
            'settings': settings
        })
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/settings', methods=['POST'])
def update_settings():
    """Update settings - Updated to support dashboard settings"""
    try:
        data = request.get_json()
        settings = load_settings()

        # Handle dashboard settings from your JavaScript
        dashboard_fields = ['title', 'subtitle', 'organization', 'primaryColor',
                          'showTitle', 'showSubtitle', 'showOrg', 'showRSSI',
                          'showBattery', 'autoRefresh']

        dashboard_settings = {}
        for field in dashboard_fields:
            if field in data:
                dashboard_settings[field] = data[field]

        if dashboard_settings:
            if 'dashboard_settings' not in settings:
                settings['dashboard_settings'] = {}
            settings['dashboard_settings'].update(dashboard_settings)

        # Handle timezone settings (existing functionality)
        if 'timezone' in data:
            timezone_str = data.get('timezone', 'UTC')
            try:
                pytz.timezone(timezone_str)
                settings['timezone'] = timezone_str
                session['timezone'] = timezone_str
            except pytz.exceptions.UnknownTimeZoneError:
                return jsonify({'error': 'Invalid timezone'}), 400

        # Handle refresh interval (existing functionality)
        if 'refresh_interval' in data:
            refresh_interval = data.get('refresh_interval', 30)
            if not isinstance(refresh_interval, int) or refresh_interval < 5 or                                                                                                              refresh_interval > 300:
                return jsonify({'error': 'Refresh interval must be between 5 and                                                                                                              300 seconds'}), 400
            settings['refresh_interval'] = refresh_interval
            session['refresh_interval'] = refresh_interval

        # Handle dashboard settings if provided
        if 'dashboard' in data:
            if 'dashboard' not in settings:
                settings['dashboard'] = {}
            settings['dashboard'].update(data['dashboard'])

        # Save to file
        if save_settings(settings):
            return jsonify({
                'status': 'success',  # This is what your JavaScript expects!
                'message': 'Settings updated successfully'
            })
        else:
            return jsonify({'status': 'error', 'message': 'Failed to save settin                                                                                                             gs'}), 500

    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)}), 500

@app.route('/api/settings', methods=['DELETE'])
def reset_settings():
    """Reset settings to defaults"""
    try:
        # Reset to default settings
        default_settings = DEFAULT_SETTINGS.copy()
        default_settings['dashboard_settings'] = {
            'title': 'LoRa Sensor Network',
            'subtitle': 'Environmental Data Dashboard',
            'organization': '',
            'primaryColor': '#3b82f6',
            'showTitle': True,
# gateway_timestamp = data.get('timestamp')  # Match your JSON field name
            'showSubtitle': True,
            'showOrg': False,
            'showRSSI': True,
            'showBattery': True,
            'autoRefresh': True
        }

        if save_settings(default_settings):
            # Clear session
            session.clear()
            return jsonify({
                'status': 'success',
                'message': 'Settings reset to defaults'
            })
        else:
            return jsonify({'status': 'error', 'message': 'Failed to reset setti                                                                                                             ngs'}), 500

    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)}), 500

@app.route('/api/sensor-data', methods=['POST'])
def receive_sensor_data():
    app.logger.info("=== SENSOR DATA ENDPOINT HIT ===")

    try:
        data = request.get_json()

        # ADD DEBUG CODE HERE:
        print(f"DEBUG: Available fields: {list(data.keys())}")
        print(f"DEBUG: timestamp field: {data.get('timestamp')}")
        print(f"DEBUG: lastUpdate field: {data.get('lastUpdate')}")

        data = request.get_json()
        app.logger.info(f"Received data: {data}")

        gateway_timestamp = data.get('timestamp')  # Match your JSON field name
        app.logger.info(f"lastUpdate field: {gateway_timestamp}")

        # Write to file for debugging
        with open('/app/debug.log', 'a') as f:
            f.write(f"=== {datetime.utcnow()} ===\n")
            f.write(f"Gateway lastUpdate: {gateway_timestamp}\n")

        # Log when we receive the request
        receive_time = datetime.utcnow()
        print(f"Backend received request at: {receive_time.strftime('%H:%M:%S')}                                                                                                             ")
        print(f"Gateway lastUpdate: {gateway_timestamp}")

        # Extract sensor data
        node_id = data.get('node_id')
        temperature = data.get('temperature_f')
        humidity = data.get('humidity')
        pressure = data.get('pressure_hpa')
        battery_voltage = data.get('battery_voltage')
        rssi = data.get('rssi')
        snr = data.get('snr')

        # Convert F to C for database storage
        if temperature:
            temperature = (temperature - 32) * 5/9

        # Store in database with original timestamp
        conn = sqlite3.connect(DATABASE_PATH)
        cursor = conn.cursor()

        if gateway_timestamp:
            # Convert gateway timestamp format to SQLite format
            try:
                dt = datetime.strptime(gateway_timestamp, "%Y-%m-%d  %H:%M:%S")
                sqlite_timestamp = dt.strftime("%Y-%m-%d  %H:%M:%S")
            except:
                sqlite_timestamp = gateway_timestamp

            cursor.execute('''
                INSERT INTO sensor_data
                (node_id, temperature, humidity, pressure, battery_voltage, rssi                                                                                                             , snr, timestamp)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            ''', (node_id, temperature, humidity, pressure, battery_voltage, rss                                                                                                             i, snr, sqlite_timestamp))
        else:
            print("No gateway timestamp, using server time")
            cursor.execute('''
                INSERT INTO sensor_data
                (node_id, temperature, humidity, pressure, battery_voltage, rssi                                                                                                             , snr)
                VALUES (?, ?, ?, ?, ?, ?, ?)
            ''', (node_id, temperature, humidity, pressure, battery_voltage, rss                                                                                                             i, snr))

        conn.commit()

        # Verify what was actually stored
        cursor.execute('SELECT timestamp FROM sensor_data WHERE id = last_insert                                                                                                             _rowid()')
        stored_timestamp = cursor.fetchone()[0]
        print(f"Actually stored in DB: {stored_timestamp}")

        conn.close()

        return jsonify({
            'success': True,
            'message': 'Sensor data received',
            'timestamp': gateway_timestamp


        })

    except Exception as e:
        print(f"Error in receive_sensor_data: {e}")
        return jsonify({'error': str(e)}), 500

@app.route('/api/sensor-data/latest', methods=['GET'])
def get_latest_sensor_data():
    """Get latest sensor data for all nodes"""
    try:
        user_tz = get_user_timezone()

        conn = sqlite3.connect(DATABASE_PATH)
        conn.row_factory = sqlite3.Row
        cursor = conn.cursor()

        # Get latest data for each node
        cursor.execute('''
            SELECT s1.* FROM sensor_data s1
            INNER JOIN (
                SELECT node_id, MAX(timestamp) as max_timestamp
                FROM sensor_data
                GROUP BY node_id
            ) s2 ON s1.node_id = s2.node_id AND s1.timestamp = s2.max_timestamp
            ORDER BY s1.timestamp DESC
        ''')

        rows = cursor.fetchall()
        latest_data = []

        for row in rows:
            # Don't use format_timestamp_for_user - it's causing the malformed o                                                                                                             utput
            # Just clean up the double space and use the stored timestamp direct                                                                                                             ly
            clean_timestamp = row['timestamp'].replace('  ', ' ')  # Remove doub                                                                                                             le space

            # Convert temperature back to Fahrenheit for display
            temp_f = None
            if row['temperature']:
                temp_f = (row['temperature'] * 9/5) + 32

            latest_data.append({
                'id': row['id'],
                'node_id': row['node_id'],
                'temperature': row['temperature'],
                'temperature_f': round(temp_f, 1) if temp_f else None,
                'humidity': row['humidity'],
                'pressure': row['pressure'],
                'battery_voltage': row['battery_voltage'],
                'rssi': row['rssi'],
                'snr': row['snr'],
                'timestamp': clean_timestamp  # Use clean timestamp directly
            })

        conn.close()
        return jsonify({
            'success': True,
            'data': latest_data,
            'count': len(latest_data),
            'timezone': user_tz
        })

    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/sensor-data/history', methods=['GET'])
def get_sensor_history():
    """Get sensor data history with timezone support"""
    try:
        node_id = request.args.get('node_id')
        hours = int(request.args.get('hours', 24))
        user_tz = get_user_timezone()

        conn = sqlite3.connect(DATABASE_PATH)
        conn.row_factory = sqlite3.Row
        cursor = conn.cursor()

        query = '''
            SELECT * FROM sensor_data
            WHERE timestamp >= datetime('now', '-{} hours')
        '''.format(hours)

        if node_id:
            query += ' AND node_id = ?'
            cursor.execute(query + ' ORDER BY timestamp DESC', (node_id,))
        else:
            cursor.execute(query + ' ORDER BY timestamp DESC')

        rows = cursor.fetchall()

        history = []
        for row in rows:
            timestamp_info = format_timestamp_for_user(row['timestamp'], user_tz                                                                                                             )

            history.append({
                'id': row['id'],
                'node_id': row['node_id'],
                'temperature': row['temperature'],
                'humidity': row['humidity'],
                'pressure': row['pressure'],
                'battery_voltage': row['battery_voltage'],
                'rssi': row['rssi'],
                'snr': row['snr'],
                'timestamp': timestamp_info
            })

        conn.close()

        return jsonify({
            'success': True,
            'data': history,
            'count': len(history),
            'timezone': user_tz,
            'hours': hours
        })

    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/network/stats', methods=['GET'])
def get_network_stats():
    """Get network statistics"""
    try:
        user_tz = get_user_timezone()

        conn = sqlite3.connect(DATABASE_PATH)
        cursor = conn.cursor()

        # Total messages
        cursor.execute('SELECT COUNT(*) as total FROM sensor_data')
        total_messages = cursor.fetchone()[0]

        # Active nodes (nodes that sent data in last hour)
        cursor.execute('''
            SELECT COUNT(DISTINCT node_id) as active
            FROM sensor_data
            WHERE timestamp >= datetime('now', '-1 hour')
        ''')
        active_nodes = cursor.fetchone()[0]

        # Average RSSI
        cursor.execute('''
            SELECT AVG(rssi) as avg_rssi
            FROM sensor_data
            WHERE timestamp >= datetime('now', '-24 hours')
        ''')
        avg_rssi = cursor.fetchone()[0] or 0

        # Last update
        cursor.execute('''
            SELECT MAX(timestamp) as last_update
            FROM sensor_data
        ''')
        last_update = cursor.fetchone()[0]

        conn.close()

        # Format last update timestamp
        last_update_info = None
        if last_update:
            last_update_info = format_timestamp_for_user(last_update, user_tz)

        return jsonify({
            'success': True,
            'stats': {
                'total_messages': total_messages,
                'active_nodes': active_nodes,
                'avg_rssi': round(avg_rssi, 1),
                'success_rate': 98.2,  # This would be calculated based on expec                                                                                                             ted vs received
                'uptime': '7d 12h',    # This would be calculated from server st                                                                                                             art time
                'last_update': last_update_info
            },
            'timezone': user_tz
        })

    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/timezone/validate', methods=['POST'])
def validate_timezone():
    """Validate a timezone string"""
    try:
        data = request.get_json()
        timezone_str = data.get('timezone', '')

        try:
            # Test if timezone is valid
            tz = pytz.timezone(timezone_str)

            # Get current time in that timezone
            utc_now = datetime.utcnow().replace(tzinfo=pytz.UTC)
            local_time = utc_now.astimezone(tz)

            return jsonify({
                'valid': True,
                'timezone': timezone_str,
                'current_time': local_time.strftime('%Y-%m-%d %H:%M:%S %Z'),
                'offset': local_time.strftime('%z')
            })

        except pytz.exceptions.UnknownTimeZoneError:
            return jsonify({
                'valid': False,
                'error': 'Unknown timezone'
            })

    except Exception as e:
        return jsonify({
            'valid': False,
            'error': str(e)
        }), 500

@app.route('/dashboard')
@app.route('/')
def dashboard():
    """Serve the main dashboard"""
    try:
        # Serve static dashboard file
        return app.send_static_file('dashboard.html')
    except Exception as e:
        return jsonify({'error': 'Dashboard not found. Make sure static/dashboar                                                                                                             d.html exists.'}), 404

@app.route('/charts')
def charts():
    """Serve the charts page"""
    try:
        return app.send_static_file('charts.html')
    except:
        return jsonify({'error': 'Charts not found. Make sure static/charts.html                                                                                                              exists.'}), 404

@app.route('/health')
def health_check():
    """Health check endpoint"""
    return jsonify({
        'status': 'healthy',
        'timestamp': datetime.utcnow().isoformat(),
        'version': '1.0.0'
    })

# Error handlers
@app.errorhandler(404)
def not_found(error):
    return jsonify({'error': 'Endpoint not found'}), 404

@app.errorhandler(500)
def internal_error(error):
    return jsonify({'error': 'Internal server error'}), 500

# Initialize app
if __name__ == '__main__':
    # Initialize database on startup
    init_database()

    # Load initial settings
    settings = load_settings()
    print(f"Server starting with timezone: {settings.get('timezone', 'UTC')}")

    # Run the app
    app.run(
        host='0.0.0.0',
        port=int(os.environ.get('PORT', 5001)),
        debug=os.environ.get('DEBUG', 'False').lower() == 'true'
    )
