#!/usr/bin/env python3
"""
Database initialization script for LoRa Sensor Network
This script creates the SQLite database and tables if they don't exist.
"""

import sqlite3
import os
import json
from datetime import datetime

# Configuration
DATABASE_PATH = os.environ.get('DB_PATH', '/app/data/sensors.db')
CONFIG_PATH = os.environ.get('CONFIG_PATH', '/app/config')

def init_database():
    """Initialize SQLite database with required tables"""
    try:
        # Ensure data directory exists
        os.makedirs(os.path.dirname(DATABASE_PATH), exist_ok=True)
        
        print(f"Initializing database at: {DATABASE_PATH}")
        
        conn = sqlite3.connect(DATABASE_PATH)
        cursor = conn.cursor()

        # Create sensor_data table (matches your app.py structure)
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

        # Create indexes for better performance
        cursor.execute('''
            CREATE INDEX IF NOT EXISTS idx_sensor_data_node_id 
            ON sensor_data(node_id)
        ''')
        
        cursor.execute('''
            CREATE INDEX IF NOT EXISTS idx_sensor_data_timestamp 
            ON sensor_data(timestamp)
        ''')

        conn.commit()
        conn.close()
        
        print("✅ Database tables created successfully")
        return True
        
    except Exception as e:
        print(f"❌ Database initialization error: {e}")
        return False

def init_config():
    """Initialize configuration files"""
    try:
        # Ensure config directory exists
        os.makedirs(CONFIG_PATH, exist_ok=True)
        
        # Create default settings.json if it doesn't exist
        settings_file = os.path.join(CONFIG_PATH, 'settings.json')
        if not os.path.exists(settings_file):
            default_settings = {
                "timezone": "UTC",
                "refresh_interval": 30,
                "dashboard": {
                    "title": "LoRa Sensor Network",
                    "theme": "default"
                },
                "dashboard_settings": {
                    "title": "LoRa Sensor Network",
                    "subtitle": "Environmental Data Dashboard",
                    "organization": "",
                    "primaryColor": "#3b82f6",
                    "showTitle": True,
                    "showSubtitle": True,
                    "showOrg": False,
                    "showRSSI": True,
                    "showBattery": True,
                    "autoRefresh": True
                }
            }
            
            with open(settings_file, 'w') as f:
                json.dump(default_settings, f, indent=2)
            
            print(f"✅ Created default settings: {settings_file}")
        
        # Create default nodes.json if it doesn't exist
        nodes_file = os.path.join(CONFIG_PATH, 'nodes.json')
        if not os.path.exists(nodes_file):
            default_nodes = {
                "nodes": [
                    {"id": "1001", "name": "Basement"},
                    {"id": "1002", "name": "Attic"},
                    {"id": "1003", "name": "Garage"}
                ]
            }
            
            with open(nodes_file, 'w') as f:
                json.dump(default_nodes, f, indent=2)
            
            print(f"✅ Created default nodes config: {nodes_file}")
        
        return True
        
    except Exception as e:
        print(f"❌ Config initialization error: {e}")
        return False

if __name__ == '__main__':
    print("🚀 Initializing LoRa Sensor Network...")
    print(f"📁 Database path: {DATABASE_PATH}")
    print(f"⚙️  Config path: {CONFIG_PATH}")
    
    # Initialize database
    if init_database():
        print("✅ Database initialization complete")
    else:
        print("❌ Database initialization failed")
        exit(1)
    
    # Initialize configuration
    if init_config():
        print("✅ Configuration initialization complete")
    else:
        print("❌ Configuration initialization failed")
        exit(1)
    
    print("🎯 Initialization complete - ready to start API server!")