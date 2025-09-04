/*
  EoRa Pi LoRa Gateway with Digital Ocean Integration
  Sends BME280 sensor data to your Digital Ocean server
  Perfect for T-Mobile Internet (no port forwarding needed!)
*/

#include <RadioLib.h>
#define EoRa_PI_V1
#include "boards.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WiFiManager.h>
#include <time.h>
#include <Ticker.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#define RADIOLIB_SX126X_SYNC_WORD_PUBLIC 0x34

// ====== DIGITAL OCEAN CONFIGURATION ======
// Replace YOUR_DROPLET_IP with your actual Digital Ocean droplet IP
const String DIGITAL_OCEAN_ENDPOINT = "http://YOUR_DROPLET_IP:5000/api/sensor-data";
const String BACKUP_ENDPOINT = "https://webhook.site/your-unique-id";  // Backup option
const String API_KEY = "lora-sensor-2025-secure";  // Match your server's API key
const bool ENABLE_BACKUP_ENDPOINT = true;  // Send to backup if primary fails
const bool ENABLE_LOCAL_STORAGE = true;   // Keep local files as backup

// LoRa Parameters - Match your sensor nodes
uint8_t txPower = 22;
float radioFreq = 915.0;
#define LORA_PREAMBLE_LENGTH 512
#define LORA_SPREADING_FACTOR 7
#define LORA_BANDWIDTH 125.0
#define LORA_CODINGRATE 7

// Scheduled transmission settings
const unsigned long TRANSMISSION_INTERVAL = 900000;  // 15 minutes
const unsigned long RESPONSE_TIMEOUT = 30000;        // 30 seconds per node
const unsigned long RETRY_DELAY = 5000;              // 5 seconds retry delay

// Node management
String activeNodes[] = {"NODE01", "NODE02", "NODE03", "NODE04", "NODE05"};
const int MAX_NODES = sizeof(activeNodes) / sizeof(activeNodes[0]);

// Communication state
enum CommState { STATE_IDLE, STATE_SENDING_WOR, STATE_SENDING_REQUEST, STATE_WAITING_RESPONSE };
CommState currentState = STATE_IDLE;
unsigned long lastScheduledTx = 0;
unsigned long stateStartTime = 0;
bool collectionInProgress = false;

// Network and hardware
WiFiManager wm;
AsyncWebServer server(80);
SX1262 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);

#define TZ "EST+5EDT,M3.2.0/2,M11.1.0/2"
char strftime_buf[64];
String dtStamp;

// Statistics tracking
struct GatewayStats {
  unsigned long totalCollections;
  unsigned long successfulUploads;
  unsigned long failedUploads;
  unsigned long nodesResponded;
  unsigned long lastUploadTime;
  String lastError;
  bool digitalOceanOnline;
  float lastRoundTripTime;
};

GatewayStats gatewayStats = {0, 0, 0, 0, 0, "", true, 0.0};

// BME280 data structure
struct BME280Data {
  float temperature;
  float humidity;
  float pressure;
  float heat_index;
  float dew_point;
  String timestamp;
  String nodeId;
  String gatewayTimestamp;
  float rssi;
  float snr;
  bool valid;
};

String getDateTime() {
  struct tm *ti;
  time_t tnow = time(nullptr);
  ti = localtime(&tnow);
  strftime(strftime_buf, sizeof(strftime_buf), "%a-%m-%d-%Y--%H:%M:%S", localtime(&tnow));
  dtStamp = String(strftime_buf);
  return dtStamp;
}

// ============== BME280 DATA PROCESSING ==============

float calculateHeatIndex(float temp_f, float humidity) {
  if (temp_f < 80.0) return temp_f;
  float hi = 0.5 * (temp_f + 61.0 + ((temp_f - 68.0) * 1.2) + (humidity * 0.094));
  if (hi > 79) {
    hi = -42.379 + 2.04901523 * temp_f + 10.14333127 * humidity
         - 0.22475541 * temp_f * humidity - 0.00683783 * temp_f * temp_f
         - 0.05481717 * humidity * humidity + 0.00122874 * temp_f * temp_f * humidity
         + 0.00085282 * temp_f * humidity * humidity - 0.00000199 * temp_f * temp_f * humidity * humidity;
  }
  return hi;
}

float calculateDewPoint(float temp_c, float humidity) {
  float a = 17.27;
  float b = 237.7;
  float alpha = ((a * temp_c) / (b + temp_c)) + log(humidity / 100.0);
  return (b * alpha) / (a - alpha);
}

BME280Data parseBME280Data(String payload) {
  BME280Data data;
  data.valid = false;
  data.gatewayTimestamp = getDateTime();
  data.rssi = radio.getRSSI();
  data.snr = radio.getSNR();
  
  Serial.println("Raw payload: " + payload);
  
  // Expected format: "NodeID,timestamp,temp_c,humidity,pressure_hpa"
  int commaIndex1 = payload.indexOf(',');
  int commaIndex2 = payload.indexOf(',', commaIndex1 + 1);
  int commaIndex3 = payload.indexOf(',', commaIndex2 + 1);
  int commaIndex4 = payload.indexOf(',', commaIndex3 + 1);
  
  if (commaIndex1 > 0 && commaIndex2 > 0 && commaIndex3 > 0 && commaIndex4 > 0) {
    data.nodeId = payload.substring(0, commaIndex1);
    data.timestamp = payload.substring(commaIndex1 + 1, commaIndex2);
    
    float temp_c = payload.substring(commaIndex2 + 1, commaIndex3).toFloat();
    data.humidity = payload.substring(commaIndex3 + 1, commaIndex4).toFloat();
    data.pressure = payload.substring(commaIndex4 + 1).toFloat();
    
    data.temperature = (temp_c * 9.0/5.0) + 32.0;
    data.heat_index = calculateHeatIndex(data.temperature, data.humidity);
    data.dew_point = calculateDewPoint(temp_c, data.humidity);
    
    data.valid = true;
    
    Serial.printf("✅ Parsed data from %s: %.1f°F, %.1f%%, %.2f hPa (RSSI: %.1f dBm)\n",
                  data.nodeId.c_str(), data.temperature, data.humidity, data.pressure, data.rssi);
  } else {
    Serial.println("❌ Invalid payload format");
  }
  
  return data;
}

// ============== DIGITAL OCEAN INTEGRATION ==============

bool sendToDigitalOcean(BME280Data data) {
  if (!data.valid) return false;
  
  HTTPClient http;
  unsigned long startTime = millis();
  
  Serial.printf("📡 Sending %s data to Digital Ocean...\n", data.nodeId.c_str());
  
  // Configure HTTP client
  http.begin(DIGITAL_OCEAN_ENDPOINT);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("User-Agent", "LoRa-Gateway-ESP32/1.0");
  http.addHeader("X-Gateway-ID", "LoRa_Gateway_TMobile");
  
  // Add API key if configured
  if (API_KEY.length() > 5) {
    http.addHeader("X-API-Key", API_KEY);
  }
  
  // Create JSON payload
  DynamicJsonDocument doc(1024);
  doc["node_id"] = data.nodeId;
  doc["gateway_timestamp"] = data.gatewayTimestamp;
  doc["node_timestamp"] = data.timestamp;
  doc["temperature_f"] = round(data.temperature * 10.0) / 10.0;  // Round to 1 decimal
  doc["humidity"] = round(data.humidity * 10.0) / 10.0;
  doc["pressure_hpa"] = round(data.pressure * 100.0) / 100.0;   // Round to 2 decimals
  doc["heat_index"] = round(data.heat_index * 10.0) / 10.0;
  doc["dew_point"] = round(data.dew_point * 10.0) / 10.0;
  doc["rssi"] = round(data.rssi * 10.0) / 10.0;
  doc["snr"] = round(data.snr * 10.0) / 10.0;
  doc["collection_cycle"] = getCollectionCycle();
  doc["gateway_id"] = "LoRa_Gateway_TMobile";
  
  String jsonPayload;
  serializeJson(doc, jsonPayload);
  
  Serial.printf("JSON payload: %s\n", jsonPayload.c_str());
  
  // Send POST request
  int httpCode = http.POST(jsonPayload);
  unsigned long requestTime = millis() - startTime;
  gatewayStats.lastRoundTripTime = requestTime;
  
  if (httpCode > 0) {
    String response = http.getString();
    
    if (httpCode >= 200 && httpCode < 300) {
      Serial.printf("✅ Digital Ocean upload successful! HTTP %d (%.1fs)\n", httpCode, requestTime / 1000.0);
      Serial.printf("Response: %s\n", response.c_str());
      
      gatewayStats.successfulUploads++;
      gatewayStats.lastUploadTime = millis();
      gatewayStats.digitalOceanOnline = true;
      gatewayStats.lastError = "";
      
      http.end();
      return true;
    } else {
      Serial.printf("❌ Digital Ocean returned HTTP %d: %s\n", httpCode, response.c_str());
      gatewayStats.lastError = "HTTP " + String(httpCode) + ": " + response;
    }
  } else {
    String error = http.errorToString(httpCode);
    Serial.printf("❌ Digital Ocean request failed: %s\n", error.c_str());
    gatewayStats.lastError = "Request failed: " + error;
    gatewayStats.digitalOceanOnline = false;
  }
  
  http.end();
  gatewayStats.failedUploads++;
  
  return false;
}

bool sendToBackupEndpoint(BME280Data data) {
  if (!ENABLE_BACKUP_ENDPOINT || BACKUP_ENDPOINT.length() < 10) return false;
  
  HTTPClient http;
  http.begin(BACKUP_ENDPOINT);
  http.addHeader("Content-Type", "application/json");
  
  // Simplified payload for backup
  DynamicJsonDocument doc(512);
  doc["node"] = data.nodeId;
  doc["temp"] = data.temperature;
  doc["humidity"] = data.humidity;
  doc["pressure"] = data.pressure;
  doc["rssi"] = data.rssi;
  doc["time"] = data.gatewayTimestamp;
  doc["source"] = "LoRa_Gateway_Backup";
  
  String jsonPayload;
  serializeJson(doc, jsonPayload);
  
  int httpCode = http.POST(jsonPayload);
  bool success = (httpCode >= 200 && httpCode < 300);
  
  if (success) {
    Serial.printf("✅ Backup endpoint upload successful! HTTP %d\n", httpCode);
  } else {
    Serial.printf("❌ Backup endpoint failed: HTTP %d\n", httpCode);
  }
  
  http.end();
  return success;
}

int getCollectionCycle() {
  struct tm *ti;
  time_t tnow = time(nullptr);
  ti = localtime(&tnow);
  return (ti->tm_hour * 4) + (ti->tm_min / 15);  // 0-95 (15-min cycles per day)
}

// ============== LOCAL FILE BACKUP ==============

bool saveToLocalFile(BME280Data data) {
  if (!ENABLE_LOCAL_STORAGE || !data.valid) return false;
  
  // Create filename based on date
  struct tm *ti;
  time_t tnow = time(nullptr);
  ti = localtime(&tnow);
  
  char dateStr[12];
  strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", ti);
  
  String filename = "/lora_backup_" + String(dateStr) + ".csv";
  
  // Check if file exists, create header if new
  bool fileExists = LittleFS.exists(filename);
  
  File file = LittleFS.open(filename, "a");
  if (!file) {
    Serial.printf("❌ Failed to open local file: %s\n", filename.c_str());
    return false;
  }
  
  // Write header if new file
  if (!fileExists) {
    file.println("gateway_time,node_id,node_time,temp_f,humidity,pressure_hpa,rssi,snr");
  }
  
  // Write data record
  String record = data.gatewayTimestamp + "," +
                  data.nodeId + "," +
                  data.timestamp + "," +
                  String(data.temperature, 1) + "," +
                  String(data.humidity, 1) + "," +
                  String(data.pressure, 2) + "," +
                  String(data.rssi, 1) + "," +
                  String(data.snr, 1);
  
  file.println(record);
  file.close();
  
  Serial.printf("💾 Saved %s data to local file\n", data.nodeId.c_str());
  return true;
}

// ============== DATA PROCESSING PIPELINE ==============

void processReceivedSensorData(BME280Data data) {
  if (!data.valid) return;
  
  Serial.printf("\n🌡️  === PROCESSING %s DATA ===\n", data.nodeId.c_str());
  
  bool primarySuccess = false;
  bool backupSuccess = false;
  bool localSuccess = false;
  
  // 1. Try Digital Ocean (primary)
  primarySuccess = sendToDigitalOcean(data);
  
  // 2. Try backup endpoint if primary failed
  if (!primarySuccess && ENABLE_BACKUP_ENDPOINT) {
    Serial.println("🔄 Primary failed, trying backup endpoint...");
    backupSuccess = sendToBackupEndpoint(data);
  }
  
  // 3. Always save locally for backup
  if (ENABLE_LOCAL_STORAGE) {
    localSuccess = saveToLocalFile(data);
  }
  
  // Update statistics
  gatewayStats.nodesResponded++;
  
  // Summary
  Serial.printf("📊 Upload Summary for %s:\n", data.nodeId.c_str());
  Serial.printf("   Digital Ocean: %s\n", primarySuccess ? "✅ Success" : "❌ Failed");
  if (ENABLE_BACKUP_ENDPOINT) {
    Serial.printf("   Backup Endpoint: %s\n", backupSuccess ? "✅ Success" : (primarySuccess ? "⏭️ Skipped" : "❌ Failed"));
  }
  if (ENABLE_LOCAL_STORAGE) {
    Serial.printf("   Local File: %s\n", localSuccess ? "✅ Success" : "❌ Failed");
  }
  
  if (primarySuccess || backupSuccess || localSuccess) {
    Serial.printf("✅ %s data processed successfully!\n", data.nodeId.c_str());
  } else {
    Serial.printf("⚠️  %s data processing failed on all endpoints!\n", data.nodeId.c_str());
  }
  
  Serial.println("=== END DATA PROCESSING ===\n");
}

// ============== LORA COMMUNICATION ==============

bool isScheduledTransmissionTime() {
  unsigned long currentTime = millis();
  return (lastScheduledTx == 0 || (currentTime - lastScheduledTx >= TRANSMISSION_INTERVAL));
}

void sendWORPacket() {
  String wakeonRadio = "3,WOR--1234567890qwerty";
  Serial.println("🚨 === SENDING WOR PACKET ===");
  
  int state = radio.transmit(wakeonRadio);
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("✅ WOR transmitted successfully");
  } else {
    Serial.printf("❌ WOR failed: %d\n", state);
  }
  delay(100);
}

void sendDataRequestPacket() {
  String timestamp = getDateTime();
  String dataRequest = "1," + timestamp;
  Serial.printf("📊 === REQUESTING DATA ===\n");
  
  int state = radio.transmit(dataRequest);
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("✅ Data request sent");
  } else {
    Serial.printf("❌ Data request failed: %d\n", state);
  }
  delay(100);
}

bool checkForSensorResponse() {
  String receivedData;
  int state = radio.startReceive();
  delay(500);
  state = radio.readData(receivedData);
  
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("📡 === SENSOR RESPONSE RECEIVED ===");
    BME280Data sensorData = parseBME280Data(receivedData);
    
    if (sensorData.valid) {
      processReceivedSensorData(sensorData);
      return true;
    }
  } else if (state != RADIOLIB_ERR_RX_TIMEOUT) {
    Serial.printf("Receive error: %d\n", state);
  }
  
  return false;
}

void handleScheduledCollection() {
  unsigned long currentTime = millis();
  unsigned long stateElapsed = currentTime - stateStartTime;
  
  switch (currentState) {
    case STATE_IDLE:
      if (isScheduledTransmissionTime()) {
        Serial.println("⏰ === STARTING 15-MINUTE COLLECTION ===");
        currentState = STATE_SENDING_WOR;
        stateStartTime = currentTime;
        collectionInProgress = true;
        lastScheduledTx = currentTime;
        gatewayStats.totalCollections++;
      }
      break;
      
    case STATE_SENDING_WOR:
      sendWORPacket();
      currentState = STATE_SENDING_REQUEST;
      stateStartTime = currentTime;
      delay(1000);  // Brief delay between WOR and request
      break;
      
    case STATE_SENDING_REQUEST:
      sendDataRequestPacket();
      currentState = STATE_WAITING_RESPONSE;
      stateStartTime = currentTime;
      break;
      
    case STATE_WAITING_RESPONSE:
      if (checkForSensorResponse()) {
        // Got response, continue listening for more nodes
        stateStartTime = currentTime;  // Reset timeout
      } else if (stateElapsed > RESPONSE_TIMEOUT) {
        // Timeout reached, finish collection
        Serial.printf("✅ === COLLECTION CYCLE COMPLETE ===\n");
        Serial.printf("📊 Cycle Stats: %lu nodes responded, %lu successful uploads\n", 
                      gatewayStats.nodesResponded, gatewayStats.successfulUploads);
        currentState = STATE_IDLE;
        collectionInProgress = false;
      }
      break;
  }
}

// ============== WEB SERVER & DASHBOARD ==============

void setupWebServer() {
  // Main dashboard
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    String html = "<!DOCTYPE html><html><head><title>LoRa Gateway - Digital Ocean</title>";
    html += "<meta http-equiv='refresh' content='30'></head>";
    html += "<body style='font-family: Arial, sans-serif; margin: 20px;'>";
    html += "<div style='background: #007acc; color: white; padding: 15px; border-radius: 5px;'>";
    html += "<h1>🌡️ LoRa Gateway - Digital Ocean Integration</h1>";
    html += "<p>T-Mobile Internet • Last updated: " + getDateTime() + "</p>";
    html += "</div>";
    
    html += "<h2>📊 Gateway Status</h2>";
    html += "<table style='border-collapse: collapse; width: 100%;'>";
    html += "<tr><th style='border: 1px solid #ddd; padding: 8px; background: #f2f2f2;'>Metric</th>";
    html += "<th style='border: 1px solid #ddd; padding: 8px; background: #f2f2f2;'>Value</th></tr>";
    
    html += "<tr><td style='border: 1px solid #ddd; padding: 8px;'>Collection Status</td>";
    html += "<td style='border: 1px solid #ddd; padding: 8px;'>" + String(collectionInProgress ? "🟢 COLLECTING" : "🟡 IDLE") + "</td></tr>";
    
    html += "<tr><td style='border: 1px solid #ddd; padding: 8px;'>Next Collection</td>";
    html += "<td style='border: 1px solid #ddd; padding: 8px;'>" + String((TRANSMISSION_INTERVAL - (millis() - lastScheduledTx)) / 60000) + " minutes</td></tr>";
    
    html += "<tr><td style='border: 1px solid #ddd; padding: 8px;'>Total Collections</td>";
    html += "<td style='border: 1px solid #ddd; padding: 8px;'>" + String(gatewayStats.totalCollections) + "</td></tr>";
    
    html += "<tr><td style='border: 1px solid #ddd; padding: 8px;'>Successful Uploads</td>";
    html += "<td style='border: 1px solid #ddd; padding: 8px;'>" + String(gatewayStats.successfulUploads) + "</td></tr>";
    
    html += "<tr><td style='border: 1px solid #ddd; padding: 8px;'>Failed Uploads</td>";
    html += "<td style='border: 1px solid #ddd; padding: 8px;'>" + String(gatewayStats.failedUploads) + "</td></tr>";
    
    html += "<tr><td style='border: 1px solid #ddd; padding: 8px;'>Nodes Responded</td>";
    html += "<td style='border: 1px solid #ddd; padding: 8px;'>" + String(gatewayStats.nodesResponded) + "</td></tr>";
    
    html += "<tr><td style='border: 1px solid #ddd; padding: 8px;'>Digital Ocean Status</td>";
    html += "<td style='border: 1px solid #ddd; padding: 8px;'>" + String(gatewayStats.digitalOceanOnline ? "🟢 ONLINE" : "🔴 OFFLINE") + "</td></tr>";
    
    html += "<tr><td style='border: 1px solid #ddd; padding: 8px;'>Last Response Time</td>";
    html += "<td style='border: 1px solid #ddd; padding: 8px;'>" + String(gatewayStats.lastRoundTripTime) + " ms</td></tr>";
    
    if (gatewayStats.lastError.length() > 0) {
      html += "<tr><td style='border: 1px solid #ddd; padding: 8px;'>Last Error</td>";
      html += "<td style='border: 1px solid #ddd; padding: 8px; color: red;'>" + gatewayStats.lastError + "</td></tr>";
    }
    
    html += "</table>";
    
    html += "<h2>🔗 Configuration</h2>";
    html += "<p><strong>Primary Endpoint:</strong> " + DIGITAL_OCEAN_ENDPOINT + "</p>";
    if (ENABLE_BACKUP_ENDPOINT) {
      html += "<p><strong>Backup Endpoint:</strong> " + BACKUP_ENDPOINT + "</p>";
    }
    html += "<p><strong>Local Storage:</strong> " + String(ENABLE_LOCAL_STORAGE ? "Enabled" : "Disabled") + "</p>";
    html += "<p><strong>Active Nodes:</strong> ";
    for (int i = 0; i < MAX_NODES; i++) {
      if (i > 0) html += ", ";
      html += activeNodes[i];
    }
    html += "</p>";
    
    html += "<br><a href='/manual' style='background: #28a745; color: white; padding: 10px; text-decoration: none; border-radius: 5px;'>Manual Collection</a> ";
    html += "<a href='/test' style='background: #17a2b8; color: white; padding: 10px; text-decoration: none; border-radius: 5px;'>Test Digital Ocean</a> ";
    html += "<a href='/logs' style='background: #6c757d; color: white; padding: 10px; text-decoration: none; border-radius: 5px;'>View Logs</a>";
    
    html += "</body></html>";
    request->send(200, "text/html", html);
  });

  // Manual collection trigger
  server.on("/manual", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (currentState == STATE_IDLE) {
      lastScheduledTx = 0;  // Force immediate collection
      request->send(200, "text/plain", "Manual collection started! Check dashboard for progress.");
    } else {
      request->send(200, "text/plain", "Collection already in progress. Please wait...");
    }
  });

  // Test Digital Ocean connection
  server.on("/test", HTTP_GET, [](AsyncWebServerRequest *request) {
    // Create test data
    BME280Data testData;
    testData.nodeId = "TEST";
    testData.gatewayTimestamp = getDateTime();
    testData.timestamp = getDateTime();
    testData.temperature = 75.0;
    testData.humidity = 60.0;
    testData.pressure = 1013.25;
    testData.heat_index = 76.5;
    testData.dew_point = 12.0;
    testData.rssi = -85.0;
    testData.snr = 10.0;
    testData.valid = true;
    
    bool success = sendToDigitalOcean(testData);
    
    String result = success ? 
      "✅ Digital Ocean connection test SUCCESSFUL!" : 
      "❌ Digital Ocean connection test FAILED! Check logs and endpoint configuration.";
    
    request->send(200, "text/plain", result);
  });

  // Simple logs endpoint
  server.on("/logs", HTTP_GET, [](AsyncWebServerRequest *request) {
    String logs = "Gateway Logs:\n";
    logs += "=============\n";
    logs += "Current Time: " + getDateTime() + "\n";
    logs += "Uptime: " + String(millis() / 1000) + " seconds\n";
    logs += "Total Collections: " + String(gatewayStats.totalCollections) + "\n";
    logs += "Successful Uploads: " + String(gatewayStats.successfulUploads) + "\n";
    logs += "Failed Uploads: " + String(gatewayStats.failedUploads) + "\n";
    logs += "Digital Ocean Status: " + String(gatewayStats.digitalOceanOnline ? "ONLINE" : "OFFLINE") + "\n";
    
    if (gatewayStats.lastError.length() > 0) {
      logs += "Last Error: " + gatewayStats.lastError + "\n";
    }
    
    logs += "\nConfiguration:\n";
    logs += "Primary Endpoint: " + DIGITAL_OCEAN_ENDPOINT + "\n";
    logs += "Backup Enabled: " + String(ENABLE_BACKUP_ENDPOINT ? "Yes" : "No") + "\n";
    logs += "Local Storage: " + String(ENABLE_LOCAL_STORAGE ? "Yes" : "No") + "\n";
    
    request->send(200, "text/plain", logs);
  });

  // JSON API for statistics
  server.on("/api/stats", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(1024);
    doc["collection_in_progress"] = collectionInProgress;
    doc["next_collection_minutes"] = (TRANSMISSION_INTERVAL - (millis() - lastScheduledTx)) / 60000;
    doc["total_collections"] = gatewayStats.totalCollections;
    doc["successful_uploads"] = gatewayStats.successfulUploads;
    doc["failed_uploads"] = gatewayStats.failedUploads;
    doc["nodes_responded"] = gatewayStats.nodesResponded;
    doc["digital_ocean_online"] = gatewayStats.digitalOceanOnline;
    doc["last_round_trip_ms"] = gatewayStats.lastRoundTripTime;
    doc["last_error"] = gatewayStats.lastError;
    doc["uptime_seconds"] = millis() / 1000;
    doc["current_time"] = getDateTime();
    doc["endpoint"] = DIGITAL_OCEAN_ENDPOINT;
    
    String jsonString;
    serializeJson(doc, jsonString);
    request->send(200, "application/json", jsonString);
  });

  server.begin();
}

// ============== INITIALIZATION & SETUP ==============

bool initLittleFS() {
  if (!ENABLE_LOCAL_STORAGE) return true;
  
  if (!LittleFS.begin()) {
    Serial.println("❌ LittleFS initialization failed");
    return false;
  }
  
  Serial.println("✅ LittleFS initialized for local storage");
  return true;
}

void setup() {
  Serial.begin(115200);
  while(!Serial){};

  Serial.println("🚀 === LoRa Gateway with Digital Ocean Integration ===");
  Serial.println("Perfect for T-Mobile Internet - No port forwarding needed!");
  Serial.println();

  // WiFi setup
  WiFiManager wm;
  bool res = wm.autoConnect("AutoConnectAP", "password");
  if (!res) {
    Serial.println("❌ Failed to connect to WiFi");
    ESP.restart();
  } else {
    Serial.println("✅ WiFi connected");
    Serial.printf("📍 IP Address: %s\n", WiFi.localIP().toString().c_str());
  }

  // Initialize hardware
  initBoard();
  
  // Initialize file system
  initLittleFS();

  Serial.println("=== Configuration ===");
  Serial.printf("🌐 Primary Endpoint: %s\n", DIGITAL_OCEAN_ENDPOINT.c_str());
  if (ENABLE_BACKUP_ENDPOINT) {
    Serial.printf("🔄 Backup Endpoint: %s\n", BACKUP_ENDPOINT.c_str());
  }
  Serial.printf("💾 Local Storage: %s\n", ENABLE_LOCAL_STORAGE ? "Enabled" : "Disabled");
  Serial.printf("⏰ Collection Interval: %lu minutes\n", TRANSMISSION_INTERVAL / 60000);
  Serial.printf("📡 Active Nodes: ");
  for (int i = 0; i < MAX_NODES; i++) {
    if (i > 0) Serial.print(", ");
    Serial.print(activeNodes[i]);
  }
  Serial.println();
  Serial.println("===================");

  // Initialize LoRa radio
  int state = radio.begin(radioFreq, LORA_BANDWIDTH, LORA_SPREADING_FACTOR, 
                         LORA_CODINGRATE, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 
                         txPower, LORA_PREAMBLE_LENGTH, 0.0, false);

  if (state == RADIOLIB_ERR_NONE) {
    Serial.println(F("✅ LoRa gateway initialized successfully!"));
    Serial.printf("📡 Frequency: %.1f MHz, SF: %d, Power: %d dBm\n", 
                  radioFreq, LORA_SPREADING_FACTOR, txPower);
  } else {
    Serial.printf("❌ LoRa init failed, code %d\n", state);
    while (true) delay(1000);
  }

  // Setup web server
  setupWebServer();
  
  // Initialize time
  configTime();
  
  // Test Digital Ocean connection
  Serial.println("🧪 Testing Digital Ocean connection...");
  BME280Data testData;
  testData.nodeId = "STARTUP_TEST";
  testData.gatewayTimestamp = getDateTime();
  testData.timestamp = getDateTime();
  testData.temperature = 72.0;
  testData.humidity = 55.0;
  testData.pressure = 1013.25;
  testData.heat_index = 73.0;
  testData.dew_point = 10.0;
  testData.rssi = -80.0;
  testData.snr = 12.0;
  testData.valid = true;
  
  if (sendToDigitalOcean(testData)) {
    Serial.println("✅ Digital Ocean connection test successful!");
  } else {
    Serial.println("⚠️  Digital Ocean connection test failed - check configuration");
  }
  
  Serial.println();
  Serial.printf("🌐 Gateway dashboard: http://%s/\n", WiFi.localIP().toString().c_str());
  Serial.println("🚀 Ready for 15-minute scheduled sensor collections!");
  Serial.println("📱 Monitor your data at your Digital Ocean dashboard!");
  Serial.println();
}

void loop() {
  // Handle scheduled LoRa collection
  handleScheduledCollection();
  
  // Periodic maintenance
  static unsigned long lastMaintenance = 0;
  if (millis() - lastMaintenance > 3600000) {  // Every hour
    Serial.println("🧹 Hourly maintenance...");
    
    // Clean up old local files if enabled
    if (ENABLE_LOCAL_STORAGE) {
      // Could add cleanup logic here
    }
    
    // Print statistics
    Serial.printf("📊 Hourly Stats: Collections: %lu, Uploads: %lu/%lu, Nodes: %lu\n",
                  gatewayStats.totalCollections, gatewayStats.successfulUploads, 
                  gatewayStats.successfulUploads + gatewayStats.failedUploads,
                  gatewayStats.nodesResponded);
    
    lastMaintenance = millis();
  }
  
  delay(100);
}

void configTime() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", TZ, 3);
  tzset();

  Serial.print("⏰ Waiting for NTP time sync");
  while (time(nullptr) < 100000ul) {
    Serial.print(".");
    delay(2000);
  }
  Serial.println("\n✅ System Time synchronized");
  
  getDateTime();
  Serial.println("🕒 Current time: " + dtStamp);
}