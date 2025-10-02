/*
  LoRa BME280 Sensor Network
  Original concept and development: William Lucid
  AI development assistance: Claude (Anthropic) 
  
  Part of the open source LoRa BME280 Network project
  https://github.com/tech500/lora-bme280-sensor-network
  
  Hardware: EoRa-S3-900TB from EbyeIoT.com 
  
  MIT License - See LICENSE file for details
*/

/*
  LoRa BME280 Sensor Network
  Original concept and development: William Lucid
  AI development assistance: Claude (Anthropic) 
  
  Part of the open source LoRa BME280 Network project
  https://github.com/tech500/lora-bme280-sensor-network
  
  Hardware: EoRa-S3-900TB from EbyeIoT.com 
  
  MIT License - See LICENSE file for details
*/

/*
  LoRa Gateway v9 - Modified for NTP-based timing and proper node response handling
  Mains-powered coordinator for LoRa sensor network
  Sends synchronized commands and uploads data to API
*/

#include <RadioLib.h>
#define EoRa_PI_V1

#include "boards.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <time.h>

// ===== WIFI & API CONFIGURATION =====
const char* ssid = "YourSSID";
const char* password = "YourPASSWORD";
const char* apiUrl = "http://192.168.12.146:5001/api/sensor-data";

// Are we currently connected?
boolean connected = false;

// ===== NTP CONFIGURATION =====
WiFiUDP udp;
const int udpPort = 1337;
const char * udpAddress1 = "pool.ntp.org";
const char * udpAddress2 = "time.nist.gov";
#define TZ "EST+5EDT,M3.2.0/2,M11.1.0/2"

int DOW, MONTH, DATE, YEAR, HOUR, MINUTE, SECOND;
char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
char strftime_buf[64];
String timestamp(strftime_buf);
time_t tnow = 0;
String lastUpdate;

// ===== LORA CONFIGURATION =====
#define RADIO_SCLK_PIN 5
#define RADIO_MISO_PIN 3
#define RADIO_MOSI_PIN 6
#define RADIO_CS_PIN 7
#define RADIO_DIO1_PIN 33
#define RADIO_BUSY_PIN 34
#define RADIO_RST_PIN 8
#define BOARD_LED 37

SX1262 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);

// LoRa parameters (MUST MATCH SENSOR NODES!)
#define RF_FREQUENCY 915.0
#define LORA_BANDWIDTH 125.0
#define LORA_SPREADING_FACTOR 7
#define LORA_CODINGRATE 7
#define LORA_PREAMBLE_LENGTH 512
#define LORA_SYNC_WORD RADIOLIB_SX126X_SYNC_WORD_PRIVATE

// ===== NODE MANAGEMENT =====
struct SensorNode {
  uint16_t nodeID;
  String name;
  bool active;
  time_t lastSeen;
  String lastUpdate;
  float lastTemp;
  float lastHumidity;
  float lastPressure;
  float lastBattery;
  int16_t lastRSSI;
  uint32_t missedResponses;
};

// Define your sensor nodes here
SensorNode knownNodes[] = {
  {0x1001, "Basement", true, 0, "", 0, 0, 0, 0, 0, 0},
  {0x1002, "Attic", true, 0, "", 0, 0, 0, 0, 0, 0},
  {0x1003, "Garage", true, 0, "", 0, 0, 0, 0, 0, 0},
};
const int numNodes = sizeof(knownNodes) / sizeof(knownNodes[0]);

// ===== TIMING & STATISTICS =====
uint32_t cycleCounter = 0;
uint32_t totalRequests = 0;
uint32_t totalResponses = 0;
uint32_t apiUploads = 0;
int lastCollectionMinute = -1;
time_t lastStatusPrint = 0;

// ===== PACKET RECEPTION =====
volatile bool receivedFlag = false;
volatile bool enableInterrupt = true;

String jsonString;

// ===== COMMUNICATION STATE =====
enum CommState {
  IDLE,
  WAITING_FOR_RESPONSES
};
CommState currentState = IDLE;
time_t responseWaitStart = 0;
const int RESPONSE_TIMEOUT = 10; // 10 seconds

// ===== TIME FUNCTIONS (UNCHANGED) =====
void timeConfig(){
  configTime(0, 0, udpAddress1, udpAddress2);
  setenv("TZ", "EST+5EDT,M3.2.0/2,M11.1.0/2", 3);   // this sets TZ to Indianapolis, Indiana
  tzset();

  //udp only send data when connected
  if (connected)
  {
    //Send a packet
    udp.beginPacket(udpAddress1, udpPort);
    udp.printf("Seconds since boot: %u", millis() / 1000);
    udp.endPacket();
  }

  Serial.print("wait for first valid timestamp");

  while (time(nullptr) < 100000ul)
  {
    Serial.print(".");
    delay(5000);
  }

  Serial.println("\nSystem Time set\n");

  getDateTime();

  Serial.println(timestamp);
}

String getDateTime() {
  struct tm *ti;
  tnow = time(nullptr);
  ti = localtime(&tnow);
  DOW = ti->tm_wday;
  YEAR = ti->tm_year + 1900;
  MONTH = ti->tm_mon + 1;
  DATE = ti->tm_mday;
  HOUR = ti->tm_hour;
  MINUTE = ti->tm_min;
  SECOND = ti->tm_sec;
  strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%dT%H:%M:%S",ti);
  timestamp = strftime_buf;
  return (timestamp);
}

// ===== PACKET TRANSMISSION =====
void sendWORSignal() {
  String worSignal = "WOR--1234567890qwerty";
  int state = radio.transmit(worSignal);
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("📡 WOR signal sent");
    totalRequests++;
  } else {
    Serial.printf("❌ WOR send failed: %d\n", state);
  }
}

void sendDataCommand() {
  String command = lastUpdate;  
  int state = radio.transmit(command);
  if (state == RADIOLIB_ERR_NONE) {
    Serial.printf("📤 Command sent: %s\n", command.c_str());
    totalRequests++;
  } else {
    Serial.printf("❌ Command send failed: %d\n", state);
  }
}

void setFlag(void) {
  if (!enableInterrupt) return;
  receivedFlag = true;
}

// ===== WIFI FUNCTIONS =====
void connectToWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("✅ WiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    connected = true;
  } else {
    Serial.println();
    Serial.println("❌ WiFi connection failed - continuing without API");
    connected = false;
  }
}

// ===== LORA FUNCTIONS =====
void setupLoRa() {
  Serial.print("Initializing LoRa gateway... ");
  
  SPI.begin(RADIO_SCLK_PIN, RADIO_MISO_PIN, RADIO_MOSI_PIN);
  
  int state = radio.begin(
    RF_FREQUENCY,
    LORA_BANDWIDTH,
    LORA_SPREADING_FACTOR,
    LORA_CODINGRATE,
    LORA_SYNC_WORD,
    14,
    LORA_PREAMBLE_LENGTH,
    0.0,
    true
  );
  
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("success!");
  } else {
    Serial.print("failed, code ");
    Serial.println(state);
    while (true);
  }
  
  radio.setDio1Action(setFlag);
}

// ===== IMPROVED DATA PARSING =====
bool parseNodeResponse(const String& response, SensorNode& node, int16_t rssi, float snr) {
  // Expected format: "1001,temp,humidity,pressure,battery" or "1001,timestamp,temp,humidity,pressure,battery"
  
  // Clean the response string - remove extra spaces
  String cleanResponse = response;
  cleanResponse.trim();
  cleanResponse.replace(" ", "");
  
  // Count commas to determine format
  int commaCount = 0;
  for (int i = 0; i < cleanResponse.length(); i++) {
    if (cleanResponse.charAt(i) == ',') {
      commaCount++;
    }
  }
  
  // Need at least 4 commas for 5 values (nodeID + 4 sensor values)
  if (commaCount < 4) {
    Serial.printf("❌ Parse error: insufficient data - found %d commas, need 4+\n", commaCount);
    Serial.printf("   Raw packet: '%s'\n", response.c_str());
    return false;
  }
  
  // Parse the values
  String values[7]; // Allow extra room
  int valueIndex = 0;
  int startPos = 0;
  
  for (int i = 0; i <= cleanResponse.length(); i++) {
    if (i == cleanResponse.length() || cleanResponse.charAt(i) == ',') {
      if (valueIndex < 7) {
        values[valueIndex] = cleanResponse.substring(startPos, i);
        valueIndex++;
      }
      startPos = i + 1;
    }
  }
  
  // Determine if we have timestamp (6 values) or just sensor data (5 values)
  bool hasTimestamp = (valueIndex >= 6);
  int sensorStartIndex = hasTimestamp ? 2 : 1; // Skip nodeID and optionally timestamp
  
  // Validate we have enough sensor values
  int expectedValues = hasTimestamp ? 6 : 5;
  if (valueIndex < expectedValues) {
    Serial.printf("❌ Parse error: expected %d values, got %d\n", expectedValues, valueIndex);
    Serial.printf("   Raw packet: '%s'\n", response.c_str());
    return false;
  }
  
  // Extract timestamp if present
  if (hasTimestamp && values[1].length() > 0) {
    node.lastUpdate = values[1];
  }
  
  // Parse sensor data with validation
  bool parseSuccess = true;
  
  // Temperature
  float temp = values[sensorStartIndex].toFloat();
  if (temp < -50 || temp > 100) { // Reasonable range check
    Serial.printf("⚠️  Warning: Temperature out of range: %.1f°C\n", temp);
  }
  node.lastTemp = temp;
  
  // Humidity
  float humidity = values[sensorStartIndex + 1].toFloat();
  if (humidity < 0 || humidity > 100) {
    Serial.printf("⚠️  Warning: Humidity out of range: %.1f%%\n", humidity);
  }
  node.lastHumidity = humidity;
  
  // Pressure
  float pressure = values[sensorStartIndex + 2].toFloat();
  if (pressure < 800 || pressure > 1200) {
    Serial.printf("⚠️  Warning: Pressure out of range: %.1fhPa\n", pressure);
  }
  node.lastPressure = pressure;
  
  // Battery
  float battery = values[sensorStartIndex + 3].toFloat();
  if (battery < 2.0 || battery > 5.0) {
    Serial.printf("⚠️  Warning: Battery voltage out of range: %.2fV\n", battery);
  }
  node.lastBattery = battery;
  
  // Store this packet's specific RSSI/SNR values with the node
  node.lastRSSI = rssi;
  node.lastSeen = time(nullptr);
  node.missedResponses = 0;
  
  return parseSuccess;
}

// ===== API FUNCTIONS =====
bool uploadToAPI(const SensorNode& node) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("📴 No WiFi - logging %s: %.1f°C, %.1f%%, %.1fhPa, %.2fV\n", 
                  node.name.c_str(), node.lastTemp, node.lastHumidity, 
                  node.lastPressure, node.lastBattery);
    return true; // Return true so we don't treat this as an error
  }
  
  HTTPClient http;
  http.setTimeout(10000);
  http.begin(apiUrl);
  http.addHeader("Content-Type", "application/json");
  
  DynamicJsonDocument doc(1024);
  doc["node_id"] = String(node.nodeID, HEX);
  doc["node_name"] = node.name;
  doc["temperature_f"] = (node.lastTemp * 9.0/5.0) + 32.0;
  doc["humidity"] = node.lastHumidity;
  doc["pressure_hpa"] = node.lastPressure;
  doc["battery_voltage"] = node.lastBattery;
  doc["rssi"] = node.lastRSSI;
  doc["timestamp"] = lastUpdate; 
  doc["collected_by_gateway"] = true;
  doc["gateway_ip"] = WiFi.localIP().toString();

  Serial.println("doc  " + lastUpdate);
  
  serializeJson(doc, jsonString);
  
  Serial.printf("🌐 Uploading %s data to API\n", node.name.c_str());
  Serial.println(jsonString);
  
  int httpResponseCode = http.POST(jsonString);
  
  if (httpResponseCode == 200) {
    Serial.println("✅ API upload successful");
    apiUploads++;
    http.end();
    return true;
  } else {
    Serial.printf("❌ API upload failed: HTTP %d\n", httpResponseCode);
    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.printf("   Response: %s\n", response.c_str());
    }
    http.end();
    return false;
  }
}

// ===== IMPROVED RESPONSE HANDLING =====
void processIncomingData() {
  if (!receivedFlag) return;
  
  enableInterrupt = false;
  receivedFlag = false;
  
  String receivedData;
  int state = radio.readData(receivedData);
  
  if (state == RADIOLIB_ERR_NONE) {
    // Get RSSI and SNR for this specific packet
    int16_t packetRSSI = radio.getRSSI();
    float packetSNR = radio.getSNR();
    
    Serial.printf("📥 Received: %s (RSSI: %d dBm, SNR: %.1f dB)\n", 
                  receivedData.c_str(), packetRSSI, packetSNR);
    
    // Skip processing if packet is empty or too short
    if (receivedData.length() < 3) {
      Serial.printf("⚠️  Empty or too short packet, ignoring\n");
      radio.startReceive();
      enableInterrupt = true;
      return;
    }
    
    // Find which node responded by checking nodeID prefix
    bool nodeFound = false;
    for (int i = 0; i < numNodes; i++) {
      // Check both hex and decimal representations
      String nodeIDHex = String(knownNodes[i].nodeID, HEX);
      nodeIDHex.toUpperCase();
      String nodeIDDec = String(knownNodes[i].nodeID);
      
      if (receivedData.startsWith(nodeIDHex) || receivedData.startsWith(nodeIDDec)) {
        
        // Pass the packet-specific RSSI/SNR to the parser
        if (parseNodeResponse(receivedData, knownNodes[i], packetRSSI, packetSNR)) {
          Serial.printf("📊 %s: %.1f°C, %.1f%%, %.1fhPa, %.2fV (RSSI: %ddBm)\n",
                        knownNodes[i].name.c_str(), 
                        knownNodes[i].lastTemp,
                        knownNodes[i].lastHumidity, 
                        knownNodes[i].lastPressure,
                        knownNodes[i].lastBattery,
                        knownNodes[i].lastRSSI);
          
          uploadToAPI(knownNodes[i]);
          totalResponses++;
          nodeFound = true;
        } else {
          Serial.printf("❌ Failed to parse data from %s\n", knownNodes[i].name.c_str());
        }
        break;
      }
    }
    
    if (!nodeFound) {
      Serial.printf("⚠️  Unknown node or format: %s\n", receivedData.c_str());
    }
  } else {
    Serial.printf("❌ Receive error: %d\n", state);
  }
  
  // Continue listening
  radio.startReceive();
  enableInterrupt = true;
}

// ===== STATUS FUNCTIONS =====
void printGatewayStatus() {
  time_t now = time(nullptr);
  Serial.println("\n=== GATEWAY STATUS ===");
  Serial.printf("Current time: %s", ctime(&now));
  
  Serial.printf("WiFi: %s", WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf(" (RSSI: %d dBm)", WiFi.RSSI());
  }
  Serial.println();
  
  Serial.printf("LoRa: %lu sent, %lu received, %lu API uploads\n",
                totalRequests, totalResponses, apiUploads);
  Serial.printf("Success Rate: %.1f%%\n", 
                totalRequests > 0 ? (float)totalResponses / totalRequests * 100.0 : 0.0);
  
  Serial.println("\nNode Status:");
  for (int i = 0; i < numNodes; i++) {
    time_t lastSeenMinutes = knownNodes[i].lastSeen > 0 ? 
                           (now - knownNodes[i].lastSeen) / 60 : 999999;
    
    Serial.printf("  %s (%04X): %s, %.2fV, %ddBm, %ldm ago\n",
                  knownNodes[i].name.c_str(), knownNodes[i].nodeID,
                  knownNodes[i].active ? "Active" : "Inactive",
                  knownNodes[i].lastBattery, knownNodes[i].lastRSSI, lastSeenMinutes);
  }
  Serial.println("=======================\n");
}

void checkMissedNodes() {
  time_t now = time(nullptr);
  for (int i = 0; i < numNodes; i++) {
    if (knownNodes[i].active && (now - knownNodes[i].lastSeen) > 300) { // 5 minutes
      knownNodes[i].missedResponses++;
      if (knownNodes[i].missedResponses >= 3) {
        knownNodes[i].active = false;
        Serial.printf("⚠️  %s auto-disabled after 3 missed responses\n", knownNodes[i].name.c_str());
      }
    }
  }
}

// ===== MAIN SETUP =====
void setup() {
  Serial.begin(115200);
  while(!Serial){};

  Serial.println("\n🚀 LoRa Gateway v0.9 Starting...");
  
  pinMode(BOARD_LED, OUTPUT);
  digitalWrite(BOARD_LED, LOW);
  
  // Connect to WiFi
  connectToWiFi();
  
  // Setup LoRa
  setupLoRa();

  // Configure time (unchanged)
  timeConfig();

  // Start in receive mode
  radio.startReceive();
  enableInterrupt = true;
  
  Serial.println("🎯 Gateway Ready!");
  Serial.printf("📡 Monitoring %d sensor nodes\n", numNodes);
  Serial.println("⏱️  Collection every 15 minutes using NTP timing");
  Serial.println("📦 Two-packet transmission: WOR preamble + 250ms + data command");
  
  printGatewayStatus();  
}

// ===== MAIN LOOP =====
void loop() {
  time_t now = time(nullptr);

  getDateTime();

  if(MINUTE % 15 == 0 && SECOND == 0) {
    lastUpdate = timestamp; 
    Serial.print("lastUpdate:  ");
    Serial.println(lastUpdate);
    Serial.flush();
  }
  
  // Always process incoming data if available
  processIncomingData();
  
  // Handle state machine
  switch (currentState) {
    case IDLE:
      // Trigger collection at 15-minute intervals
      if (MINUTE % 15 == 0 && SECOND == 0 && MINUTE != lastCollectionMinute) {
        Serial.printf("\n🕒 Collection trigger: %02d:%02d:%02d\n", HOUR, MINUTE, SECOND);
        Serial.println(lastUpdate);
        
        // Send wake signals
        sendWORSignal();
        delay(250);
        // Send timestamp
        sendDataCommand();
        
        // Switch to waiting for responses
        currentState = WAITING_FOR_RESPONSES;
        responseWaitStart = now;
        lastCollectionMinute = MINUTE;
        cycleCounter++;
        
        Serial.println("👂 Listening for node responses...");
      }
      break;
      
    case WAITING_FOR_RESPONSES:
      // Check if timeout reached
      if (now - responseWaitStart >= RESPONSE_TIMEOUT) {
        Serial.printf("⏰ Response timeout reached (%d seconds)\n", RESPONSE_TIMEOUT);
        Serial.printf("📈 Cycle %lu complete: %lu responses received\n", cycleCounter, totalResponses);
        Serial.println("Cycle complete   " + lastUpdate);
        
        // Check for missed nodes
        checkMissedNodes(); 
        
        currentState = IDLE;
      }
      break;
  }

  // Print status every hour (3600 seconds)
  if(MINUTE == 0 && SECOND == 0){
    printGatewayStatus();
    lastStatusPrint = now;
  }
  
  // Check WiFi connection periodically
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("📶 WiFi disconnected - reconnecting...");
    connectToWiFi();
  }
  
  delay(100); // Small delay to prevent tight loop
}

