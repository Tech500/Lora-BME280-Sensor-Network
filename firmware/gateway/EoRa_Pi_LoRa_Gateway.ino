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
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <time.h>

// ===== WIFI & API CONFIGURATION =====
const char* ssid = "R2D2";
const char* password = "sissy4357";
const char* apiUrl = "http://192.168.12.146:5001/api/sensor-data";

// ===== NTP CONFIGURATION =====
WiFiUDP udp;
const int udpPort = 1337;
const char * udpAddress1 = "pool.ntp.org";
const char * udpAddress2 = "time.nist.gov";
#define TZ "EST+5EDT,M3.2.0/2,M11.1.0/2"

int DOW, MONTH, DATE, YEAR, HOUR, MINUTE, SECOND;
char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
char strftime_buf[64];
String dtStamp(strftime_buf);
time_t tnow = 0;

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
  String nodeTimestamp;
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
int16_t lastRSSI = 0;
float lastSNR = 0;

// ===== COMMUNICATION STATE =====
enum CommState {
  IDLE,
  WAITING_FOR_RESPONSES
};
CommState currentState = IDLE;
time_t responseWaitStart = 0;
const int RESPONSE_TIMEOUT = 10; // 10 seconds

//======== PACKET TRANSMISSION ========
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
  time_t now = time(nullptr);
  struct tm *timeinfo = localtime(&now);
  char timestamp[32];
  strftime(timestamp, sizeof(timestamp), "%a-%m-%d-%Y--%H:%M:%S", timeinfo);
  
  String command = String(timestamp);
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
  } else {
    Serial.println();
    Serial.println("❌ WiFi connection failed - continuing without API");
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

// ===== DATA PARSING =====
bool parseNodeResponse(const String& response, SensorNode& node) {
  // Expected format: "1001,timestamp,temp,humidity,pressure,battery"
  int commaCount = 0;
  int lastPos = 0;
  String values[6];
  
  for (int i = 0; i <= response.length(); i++) {
    if (i == response.length() || response.charAt(i) == ',') {
      if (commaCount < 6) {
        values[commaCount] = response.substring(lastPos, i);
        commaCount++;
      }
      lastPos = i + 1;
    }
    Serial.printf("❌ Parse error: expected 5+ values, got %d\n", commaCount);
    Serial.printf("   Raw packet: '%s'\n", response.c_str());  // Add this line
  }
  
  if (commaCount >= 5) { // At minimum need nodeID, temp, humidity, pressure, battery
    // values[0] should be nodeID (already matched)
    if (commaCount >= 6) node.nodeTimestamp = values[1];
    
    // Parse sensor data - adjust indices based on your node's actual format
    int dataStartIndex = (commaCount >= 6) ? 2 : 1;
    node.lastTemp = values[dataStartIndex].toFloat();
    node.lastHumidity = values[dataStartIndex + 1].toFloat();
    node.lastPressure = values[dataStartIndex + 2].toFloat();
    node.lastBattery = values[dataStartIndex + 3].toFloat();
    node.lastRSSI = lastRSSI;
    node.lastSeen = time(nullptr);
    node.missedResponses = 0;
    return true;
  }
  
  Serial.printf("❌ Parse error: expected 5+ values, got %d\n", commaCount);
  Serial.printf("   Raw data: %s\n", response.c_str());
  return false;
}

// ===== API FUNCTIONS =====
bool uploadToAPI(const SensorNode& node) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("📝 No WiFi - logging %s: %.1f°C, %.1f%%, %.1fhPa, %.2fV\n", 
                  node.name.c_str(), node.lastTemp, node.lastHumidity, 
                  node.lastPressure, node.lastBattery);
    return true;
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
  doc["timestamp"] = node.nodeTimestamp;
  doc["collected_by_gateway"] = true;
  doc["gateway_ip"] = WiFi.localIP().toString();
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  Serial.printf("🌐 Uploading %s data to API\n", node.name.c_str());
  
  int httpResponseCode = http.POST(jsonString);
  
  if (httpResponseCode == 200) {
    Serial.println("✅ API upload successful");
    apiUploads++;
    http.end();
    return true;
  } else {
    Serial.printf("❌ API upload failed: %d\n", httpResponseCode);
    http.end();
    return false;
  }
}

// ===== RESPONSE HANDLING =====
void processIncomingData() {
  if (!receivedFlag) return;
  
  enableInterrupt = false;
  receivedFlag = false;
  
  String receivedData;
  int state = radio.readData(receivedData);
  
  if (state == RADIOLIB_ERR_NONE) {
    lastRSSI = radio.getRSSI();
    lastSNR = radio.getSNR();
    
    Serial.printf("📥 Received: %s (RSSI: %d dBm, SNR: %.1f dB)\n", 
                  receivedData.c_str(), lastRSSI, lastSNR);
    
    // Find which node responded by checking nodeID prefix
    bool nodeFound = false;
    for (int i = 0; i < numNodes; i++) {
      String nodeIDStr = String(knownNodes[i].nodeID, HEX);
      nodeIDStr.toUpperCase(); // Ensure uppercase for comparison
      
      if (receivedData.startsWith(nodeIDStr) || 
          receivedData.startsWith(String(knownNodes[i].nodeID))) {
        
        if (parseNodeResponse(receivedData, knownNodes[i])) {
          Serial.printf("📊 %s: %.1f°C, %.1f%%, %.1fhPa, %.2fV\n",
                        knownNodes[i].name.c_str(), 
                        knownNodes[i].lastTemp,
                        knownNodes[i].lastHumidity, 
                        knownNodes[i].lastPressure,
                        knownNodes[i].lastBattery);
          
          uploadToAPI(knownNodes[i]);
          totalResponses++;
          nodeFound = true;
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

// ===== NTP TIME FUNCTIONS =====
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
  strftime(strftime_buf, sizeof(strftime_buf), "%a %m %d %Y  %H:%M:%S", localtime(&tnow));
  dtStamp = strftime_buf;
  dtStamp.replace(" ", "-");
  return (dtStamp);
}


/*
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
*/


// ===== MAIN SETUP =====
void setup() {
  Serial.begin(115200);
  while(!Serial){};

  Serial.println("\n🚀 LoRa Gateway v13.0 Starting...");
  
  // Configure NTP
  configTime(0, 0, udpAddress1, udpAddress2);
  setenv("TZ", TZ, 3);
  tzset();
  
  // Wait for NTP sync
  Serial.print("⏰ Waiting for NTP sync");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println();
  Serial.printf("✅ NTP synchronized: %s", ctime(&now));
  
  pinMode(BOARD_LED, OUTPUT);
  digitalWrite(BOARD_LED, LOW);
  
  // Connect to WiFi
  connectToWiFi();
  
  // Setup LoRa
  setupLoRa();
  
  // Start in receive mode
  radio.startReceive();
  enableInterrupt = true;
  
  Serial.println("🎯 Gateway Ready!");
  Serial.printf("📡 Monitoring %d sensor nodes\n", numNodes);
  Serial.println("⏱️  Collection every 3 minutes using NTP timing");
  Serial.println("🔄 Two-packet transmission: WOR preamble + 250ms + data command");
  
  printGatewayStatus();
}

// ===== MAIN LOOP =====
void loop() {
  // Update current time
  getDateTime();
  time_t now = time(nullptr);
  
  // Always process incoming data if available
  processIncomingData();
  
  // Handle state machine
  switch (currentState) {
    case IDLE:
      // Trigger collection at 3-minute intervals
      if (MINUTE % 3 == 0 && SECOND < 2 && MINUTE != lastCollectionMinute) {
        Serial.printf("\n🕒 Collection trigger: %02d:%02d:%02d\n", HOUR, MINUTE, SECOND);
        Serial.println(dtStamp);
        
        // Send wake signals
        sendWORSignal();
        delay(250);
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
        
        // Check for missed nodes
        //checkMissedNodes();  <----------------------------------------------- enable
        
        currentState = IDLE;
      }
      break;
  }
  
  // Print status every hour (3600 seconds)
  if (now - lastStatusPrint >= 3600) {
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

