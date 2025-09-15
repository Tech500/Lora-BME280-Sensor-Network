/*
  LoRa BME280 Sensor Network
  Original concept and development: William Lucid
  AI development assistance: Claude (Anthropic) 
  
  Part of the open source LoRa BME280 Network project
  https://github.com/tech500/lora-bme280-sensor-network
  
  Hardware: EoRa-S3-900TB from EbyeIoT.com 
  
  MIT License - See LICENSE file for details
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
#define GOOGLE_SCRIPT_ID "YOUR_GOOGLE_SCRIPT_ID_HERE"

// Node File System Configuration
#define ENABLE_NODE_FILES true
#define ENABLE_AUTO_WEB_REQUESTS true
const String NODE_FILE_PREFIX = "/node_";        // e.g., /node_NODE01_2025-09-03.csv
const String WEB_REQUEST_ENDPOINT = "http://your-server.com/api/sensor-data";  // Your endpoint
const unsigned long FILE_RETENTION_HOURS = 168;  // 7 days (7 * 24 hours)
const int MAX_RECORDS_PER_FILE = 1000;           // Rotate file after 1000 records

// LoRa Parameters
uint8_t txPower = 22;
float radioFreq = 915.0;
#define LORA_PREAMBLE_LENGTH 512
#define LORA_SPREADING_FACTOR 7
#define LORA_BANDWIDTH 125.0
#define LORA_CODINGRATE 7

// Scheduled transmission
const unsigned long TRANSMISSION_INTERVAL = 900000;  // 15 minutes
const unsigned long RESPONSE_TIMEOUT = 30000;        // 30 seconds per node
const unsigned long INTER_NODE_DELAY = 2000;         // 2 seconds between nodes

// Node management with individual file tracking
struct NodeFileManager {
  String nodeId;
  String currentFile;
  unsigned long recordCount;
  unsigned long totalRecords;
  bool isActive;
  unsigned long lastSeen;
  unsigned long lastFileRotation;
  float lastRSSI;
  float lastSNR;
  BME280Data lastData;
  bool hasPendingWebRequest;
  String webRequestPayload;
};

NodeFileManager nodeManagers[] = {
  {"NODE01", "", 0, 0, true, 0, 0, 0, 0, {}, false, ""},
  {"NODE02", "", 0, 0, true, 0, 0, 0, 0, {}, false, ""},
  {"NODE03", "", 0, 0, true, 0, 0, 0, 0, {}, false, ""},
  {"NODE04", "", 0, 0, true, 0, 0, 0, 0, {}, false, ""},
  {"NODE05", "", 0, 0, true, 0, 0, 0, 0, {}, false, ""}
};

const int TOTAL_MANAGED_NODES = sizeof(nodeManagers) / sizeof(nodeManagers[0]);

// Communication state
enum CommState { STATE_IDLE, STATE_SENDING_WOR, STATE_SENDING_REQUEST, STATE_WAITING_RESPONSE };
CommState currentState = STATE_IDLE;
unsigned long lastScheduledTx = 0;
unsigned long stateStartTime = 0;
int currentNodeIndex = 0;
bool collectionInProgress = false;

WiFiManager wm;
AsyncWebServer server(80);
SX1262 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);

#define TZ "EST+5EDT,M3.2.0/2,M11.1.0/2"
char strftime_buf[64];
String dtStamp;

// BME280 data structure (same as before)
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

// ============== NODE FILE MANAGEMENT ==============

// Get current filename for a specific node
String getNodeFileName(String nodeId) {
  struct tm *ti;
  time_t tnow = time(nullptr);
  ti = localtime(&tnow);
  
  char dateStr[12];
  strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", ti);
  
  return NODE_FILE_PREFIX + nodeId + "_" + String(dateStr) + ".csv";
}

// Initialize node file system
bool initNodeFileSystem() {
  if (!ENABLE_NODE_FILES) {
    Serial.println("📁 Node file system disabled");
    return false;
  }
  
  if (!LittleFS.begin()) {
    Serial.println("❌ LittleFS initialization failed");
    return false;
  }
  
  Serial.println("✅ Node file system initialized");
  
  // Initialize LoRa radio
  int state = radio.begin(radioFreq, LORA_BANDWIDTH, LORA_SPREADING_FACTOR, 
                         LORA_CODINGRATE, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 
                         txPower, LORA_PREAMBLE_LENGTH, 0.0, false);

  if (state == RADIOLIB_ERR_NONE) {
    Serial.println(F("✅ LoRa gateway initialized successfully!"));
    Serial.printf("Frequency: %.1f MHz, SF: %d, Power: %d dBm\n", 
                  radioFreq, LORA_SPREADING_FACTOR, txPower);
  } else {
    Serial.printf("❌ LoRa init failed, code %d\n", state);
    while (true) delay(1000);
  }

  // Setup web server
  setupWebServer();
  
  // Initialize time
  configTime();
  
  Serial.println("🚀 Node File Manager ready!");
  Serial.println("📁 Each sensor gets individual CSV files");
  Serial.println("📤 Auto web requests enabled for real-time data sharing");
  Serial.println("⏰ 15-minute scheduled collections starting...");
}

void loop() {
  // Handle scheduled LoRa collection
  handleScheduledCollection();
  
  // Process any pending web requests
  if (ENABLE_AUTO_WEB_REQUESTS) {
    processPendingWebRequests();
  }
  
  // Periodic maintenance (every hour)
  static unsigned long lastMaintenance = 0;
  if (millis() - lastMaintenance > 3600000) {
    if (ENABLE_NODE_FILES) {
      cleanupOldNodeFiles();
      
      // Update all node managers with fresh statistics
      for (int i = 0; i < TOTAL_MANAGED_NODES; i++) {
        nodeManagers[i].totalRecords = getTotalRecordsForNode(nodeManagers[i].nodeId);
      }
      
      Serial.println("🧹 Hourly maintenance completed");
    }
    lastMaintenance = millis();
  }
  
  delay(100);
}

void configTime() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", TZ, 3);
  tzset();

  Serial.print("Waiting for NTP time sync");
  while (time(nullptr) < 100000ul) {
    Serial.print(".");
    delay(5000);
  }
  Serial.println("\nSystem Time synchronized");
  
  getDateTime();
  Serial.println("Current time: " + dtStamp);
} each node's file manager
  for (int i = 0; i < TOTAL_MANAGED_NODES; i++) {
    NodeFileManager &mgr = nodeManagers[i];
    mgr.currentFile = getNodeFileName(mgr.nodeId);
    mgr.recordCount = countRecordsInFile(mgr.currentFile);
    mgr.totalRecords = getTotalRecordsForNode(mgr.nodeId);
    
    Serial.printf("📊 %s: Current file: %s, Records: %lu, Total: %lu\n",
                  mgr.nodeId.c_str(), mgr.currentFile.c_str(), 
                  mgr.recordCount, mgr.totalRecords);
  }
  
  // Clean up old files
  cleanupOldNodeFiles();
  
  return true;
}

// Count records in a specific file
unsigned long countRecordsInFile(String filename) {
  if (!LittleFS.exists(filename)) return 0;
  
  File file = LittleFS.open(filename, "r");
  if (!file) return 0;
  
  unsigned long count = 0;
  file.readStringUntil('\n');  // Skip header
  
  while (file.available()) {
    String line = file.readStringUntil('\n');
    if (line.length() > 10) count++;
  }
  
  file.close();
  return count;
}

// Get total records for a node across all its files
unsigned long getTotalRecordsForNode(String nodeId) {
  unsigned long total = 0;
  
  File root = LittleFS.open("/");
  File file = root.openNextFile();
  
  String searchPattern = NODE_FILE_PREFIX + nodeId + "_";
  
  while (file) {
    String fileName = file.name();
    if (fileName.startsWith(searchPattern)) {
      total += countRecordsInFile(fileName);
    }
    file.close();
    file = root.openNextFile();
  }
  root.close();
  
  return total;
}

// Save sensor data to individual node file
bool saveToNodeFile(BME280Data data) {
  if (!ENABLE_NODE_FILES || !data.valid) return false;
  
  // Find the node manager
  NodeFileManager *mgr = nullptr;
  for (int i = 0; i < TOTAL_MANAGED_NODES; i++) {
    if (nodeManagers[i].nodeId == data.nodeId) {
      mgr = &nodeManagers[i];
      break;
    }
  }
  
  if (!mgr) {
    Serial.printf("❌ No manager found for node %s\n", data.nodeId.c_str());
    return false;
  }
  
  // Check if we need to rotate the file
  String expectedFileName = getNodeFileName(data.nodeId);
  if (mgr->currentFile != expectedFileName || mgr->recordCount >= MAX_RECORDS_PER_FILE) {
    mgr->currentFile = expectedFileName;
    mgr->recordCount = 0;
    mgr->lastFileRotation = millis();
    Serial.printf("🔄 File rotation for %s: %s\n", data.nodeId.c_str(), expectedFileName.c_str());
  }
  
  // Check if file exists, create header if new
  bool fileExists = LittleFS.exists(mgr->currentFile);
  
  File dataFile = LittleFS.open(mgr->currentFile, "a");
  if (!dataFile) {
    Serial.printf("❌ Failed to open %s for writing\n", mgr->currentFile.c_str());
    return false;
  }
  
  // Write header if new file
  if (!fileExists) {
    dataFile.println("record_id,gateway_timestamp,node_timestamp,temperature_f,humidity,pressure_hpa,heat_index,dew_point,rssi,snr,collection_cycle");
    Serial.printf("📝 Created new node file: %s\n", mgr->currentFile.c_str());
  }
  
  // Generate record ID
  mgr->recordCount++;
  mgr->totalRecords++;
  String recordId = data.nodeId + "_" + String(mgr->totalRecords);
  
  // Write data record with collection cycle info
  struct tm *ti;
  time_t tnow = time(nullptr);
  ti = localtime(&tnow);
  int collectionCycle = (ti->tm_hour * 4) + (ti->tm_min / 15);  // 0-95 (15-min cycles per day)
  
  String record = recordId + "," +
                  data.gatewayTimestamp + "," + 
                  data.timestamp + "," + 
                  String(data.temperature, 1) + "," + 
                  String(data.humidity, 1) + "," + 
                  String(data.pressure, 2) + "," + 
                  String(data.heat_index, 1) + "," + 
                  String(data.dew_point, 1) + "," + 
                  String(data.rssi, 1) + "," + 
                  String(data.snr, 1) + "," + 
                  String(collectionCycle);
  
  dataFile.println(record);
  dataFile.close();
  
  // Update manager status
  mgr->lastSeen = millis();
  mgr->lastRSSI = data.rssi;
  mgr->lastSNR = data.snr;
  mgr->lastData = data;
  
  Serial.printf("💾 %s: Record #%lu saved to individual file\n", 
                data.nodeId.c_str(), mgr->recordCount);
  
  // Trigger auto web request if enabled
  if (ENABLE_AUTO_WEB_REQUESTS) {
    scheduleAutoWebRequest(*mgr, record);
  }
  
  return true;
}

// ============== AUTO WEB REQUEST SYSTEM ==============

// Schedule an automatic web request for the node's latest data
void scheduleAutoWebRequest(NodeFileManager &mgr, String csvRecord) {
  if (!ENABLE_AUTO_WEB_REQUESTS) return;
  
  // Create JSON payload from CSV record
  DynamicJsonDocument doc(1024);
  
  // Parse CSV record into JSON
  int commaPositions[15];
  int commaCount = 0;
  for (int i = 0; i < csvRecord.length() && commaCount < 15; i++) {
    if (csvRecord.charAt(i) == ',') {
      commaPositions[commaCount++] = i;
    }
  }
  
  if (commaCount >= 10) {
    doc["record_id"] = csvRecord.substring(0, commaPositions[0]);
    doc["node_id"] = mgr.nodeId;
    doc["gateway_timestamp"] = csvRecord.substring(commaPositions[0]+1, commaPositions[1]);
    doc["node_timestamp"] = csvRecord.substring(commaPositions[1]+1, commaPositions[2]);
    doc["temperature_f"] = csvRecord.substring(commaPositions[2]+1, commaPositions[3]).toFloat();
    doc["humidity"] = csvRecord.substring(commaPositions[3]+1, commaPositions[4]).toFloat();
    doc["pressure_hpa"] = csvRecord.substring(commaPositions[4]+1, commaPositions[5]).toFloat();
    doc["heat_index"] = csvRecord.substring(commaPositions[5]+1, commaPositions[6]).toFloat();
    doc["dew_point"] = csvRecord.substring(commaPositions[6]+1, commaPositions[7]).toFloat();
    doc["rssi"] = csvRecord.substring(commaPositions[7]+1, commaPositions[8]).toFloat();
    doc["snr"] = csvRecord.substring(commaPositions[8]+1, commaPositions[9]).toFloat();
    doc["collection_cycle"] = csvRecord.substring(commaPositions[9]+1).toInt();
    doc["gateway_id"] = "GATEWAY_01";  // Your gateway identifier
    
    String jsonPayload;
    serializeJson(doc, jsonPayload);
    
    mgr.webRequestPayload = jsonPayload;
    mgr.hasPendingWebRequest = true;
    
    Serial.printf("📤 Auto web request scheduled for %s\n", mgr.nodeId.c_str());
  }
}

// Process pending web requests (call this in main loop)
void processPendingWebRequests() {
  static unsigned long lastProcessTime = 0;
  if (millis() - lastProcessTime < 5000) return;  // Process every 5 seconds
  
  for (int i = 0; i < TOTAL_MANAGED_NODES; i++) {
    NodeFileManager &mgr = nodeManagers[i];
    
    if (mgr.hasPendingWebRequest && mgr.webRequestPayload.length() > 0) {
      if (sendAutoWebRequest(mgr)) {
        mgr.hasPendingWebRequest = false;
        mgr.webRequestPayload = "";
        Serial.printf("✅ Auto web request completed for %s\n", mgr.nodeId.c_str());
      } else {
        Serial.printf("🔄 Auto web request retry needed for %s\n", mgr.nodeId.c_str());
      }
    }
  }
  
  lastProcessTime = millis();
}

// Send automatic web request
bool sendAutoWebRequest(NodeFileManager &mgr) {
  if (!ENABLE_AUTO_WEB_REQUESTS || mgr.webRequestPayload.length() == 0) return false;
  
  HTTPClient http;
  http.begin(WEB_REQUEST_ENDPOINT);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Gateway-ID", "LoRa-Gateway-01");
  http.addHeader("X-Node-ID", mgr.nodeId);
  
  Serial.printf("📡 Sending auto web request for %s to %s\n", 
                mgr.nodeId.c_str(), WEB_REQUEST_ENDPOINT.c_str());
  
  int httpCode = http.POST(mgr.webRequestPayload);
  
  if (httpCode > 0) {
    String response = http.getString();
    Serial.printf("📤 Auto web request success for %s (HTTP %d): %s\n", 
                  mgr.nodeId.c_str(), httpCode, response.c_str());
    http.end();
    return (httpCode >= 200 && httpCode < 300);
  } else {
    Serial.printf("❌ Auto web request failed for %s: %s\n", 
                  mgr.nodeId.c_str(), http.errorToString(httpCode).c_str());
    http.end();
    return false;
  }
}

// ============== FILE CLEANUP AND MAINTENANCE ==============

void cleanupOldNodeFiles() {
  if (!ENABLE_NODE_FILES) return;
  
  time_t currentTime = time(nullptr);
  time_t cutoffTime = currentTime - (FILE_RETENTION_HOURS * 3600);
  
  File root = LittleFS.open("/");
  File file = root.openNextFile();
  int deletedFiles = 0;
  
  while (file) {
    String fileName = file.name();
    if (fileName.startsWith(NODE_FILE_PREFIX)) {
      time_t fileTime = file.getLastWrite();
      
      if (fileTime < cutoffTime) {
        file.close();
        if (LittleFS.remove(fileName)) {
          Serial.printf("🗑️  Deleted old node file: %s\n", fileName.c_str());
          deletedFiles++;
        }
        file = root.openNextFile();
        continue;
      }
    }
    file.close();
    file = root.openNextFile();
  }
  root.close();
  
  if (deletedFiles > 0) {
    Serial.printf("🧹 Cleaned up %d old node files\n", deletedFiles);
  }
}

// ============== BME280 DATA PROCESSING (same as before) ==============

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
  
  // Parse payload: "NodeID,timestamp,temp_c,humidity,pressure_hpa"
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
    
    Serial.printf("✅ Parsed data from %s: %.1f°F, %.1f%%, %.2f hPa\n",
                  data.nodeId.c_str(), data.temperature, data.humidity, data.pressure);
  }
  
  return data;
}

// ============== MAIN COMMUNICATION LOGIC ==============

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
    Serial.println("🌡️  === SENSOR DATA RECEIVED ===");
    BME280Data sensorData = parseBME280Data(receivedData);
    
    if (sensorData.valid) {
      // Save to individual node file
      saveToNodeFile(sensorData);
      
      // Also upload to Google Sheets (existing functionality)
      // googleSheetBME280(sensorData);  // Uncomment if you want dual storage
      
      Serial.printf("✅ %s data processed and saved\n", sensorData.nodeId.c_str());
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
        Serial.println("✅ === COLLECTION CYCLE COMPLETE ===");
        currentState = STATE_IDLE;
        collectionInProgress = false;
      }
      break;
  }
}

// ============== WEB SERVER ROUTES ==============

void setupWebServer() {
  // Main dashboard
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    String html = "<!DOCTYPE html><html><head><title>Node File Manager</title>";
    html += "<meta http-equiv='refresh' content='30'></head>";
    html += "<body><h1>LoRa Node File Manager</h1>";
    html += "<h2>Collection Status:</h2>";
    html += "<p><strong>Status:</strong> " + String(collectionInProgress ? "COLLECTING" : "IDLE") + "</p>";
    html += "<p><strong>Next Collection:</strong> " + String((TRANSMISSION_INTERVAL - (millis() - lastScheduledTx)) / 60000) + " min</p>";
    
    html += "<h2>Node Status:</h2>";
    for (int i = 0; i < TOTAL_MANAGED_NODES; i++) {
      NodeFileManager &mgr = nodeManagers[i];
      html += "<div style='border:1px solid #ccc; margin:5px; padding:10px;'>";
      html += "<strong>" + mgr.nodeId + "</strong><br>";
      html += "File: " + mgr.currentFile + "<br>";
      html += "Records: " + String(mgr.recordCount) + " (Total: " + String(mgr.totalRecords) + ")<br>";
      html += "Last seen: " + String((millis() - mgr.lastSeen) / 60000) + " min ago<br>";
      if (mgr.lastData.valid) {
        html += "Last data: " + String(mgr.lastData.temperature, 1) + "°F, ";
        html += String(mgr.lastData.humidity, 1) + "%, ";
        html += String(mgr.lastData.pressure, 2) + " hPa<br>";
      }
      html += "Web requests: " + String(mgr.hasPendingWebRequest ? "PENDING" : "UP TO DATE");
      html += "</div>";
    }
    
    html += "<br><a href='/download-all'>Download All Node Files</a> | ";
    html += "<a href='/node-stats'>Node Statistics</a> | ";
    html += "<a href='/manual'>Manual Collection</a>";
    html += "</body></html>";
    request->send(200, "text/html", html);
  });

  // Download specific node file
  server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request) {
    String nodeId = "NODE01";
    String date = "";
    
    if (request->hasParam("node")) {
      nodeId = request->getParam("node")->value();
    }
    if (request->hasParam("date")) {
      date = request->getParam("date")->value();
    }
    
    String filename = (date.length() > 0) ? 
                     (NODE_FILE_PREFIX + nodeId + "_" + date + ".csv") : 
                     getNodeFileName(nodeId);
    
    if (LittleFS.exists(filename)) {
      File file = LittleFS.open(filename, "r");
      String csvData = file.readString();
      file.close();
      
      AsyncWebServerResponse *response = request->beginResponse(200, "text/csv", csvData);
      response->addHeader("Content-Disposition", "attachment; filename=" + nodeId + "_data.csv");
      request->send(response);
    } else {
      request->send(404, "text/plain", "File not found: " + filename);
    }
  });

  // Manual collection trigger
  server.on("/manual", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (currentState == STATE_IDLE) {
      lastScheduledTx = 0;  // Force immediate collection
      request->send(200, "text/plain", "Manual collection started!");
    } else {
      request->send(200, "text/plain", "Collection already in progress...");
    }
  });

  // Node statistics JSON API
  server.on("/node-stats", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "[";
    for (int i = 0; i < TOTAL_MANAGED_NODES; i++) {
      if (i > 0) json += ",";
      NodeFileManager &mgr = nodeManagers[i];
      json += "{";
      json += "\"nodeId\":\"" + mgr.nodeId + "\",";
      json += "\"currentFile\":\"" + mgr.currentFile + "\",";
      json += "\"recordCount\":" + String(mgr.recordCount) + ",";
      json += "\"totalRecords\":" + String(mgr.totalRecords) + ",";
      json += "\"isActive\":" + String(mgr.isActive ? "true" : "false") + ",";
      json += "\"lastSeenMinutes\":" + String((millis() - mgr.lastSeen) / 60000) + ",";
      json += "\"lastRSSI\":" + String(mgr.lastRSSI, 1) + ",";
      json += "\"hasPendingWebRequest\":" + String(mgr.hasPendingWebRequest ? "true" : "false");
      json += "}";
    }
    json += "]";
    request->send(200, "application/json", json);
  });

  server.begin();
}

// ============== SETUP AND MAIN LOOP ==============

void setup() {
  Serial.begin(115200);
  while(!Serial){};

  // WiFi setup
  WiFiManager wm;
  bool res = wm.autoConnect("AutoConnectAP", "password");
  if (!res) {
    Serial.println("Failed to connect to WiFi");
  } else {
    Serial.println("WiFi connected");
  }

  initBoard();
  
  Serial.println("=== LoRa Node File Manager Starting ===");
  Serial.printf("Node files: %s\n", ENABLE_NODE_FILES ? "ENABLED" : "DISABLED");
  Serial.printf("Auto web requests: %s\n", ENABLE_AUTO_WEB_REQUESTS ? "ENABLED" : "DISABLED");
  
  // Initialize file system
  initNodeFileSystem();


  // Initialize
