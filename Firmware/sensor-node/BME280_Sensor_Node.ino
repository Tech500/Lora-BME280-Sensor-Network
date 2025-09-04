/*
  EoRa Pi BME280 Sensor Node - Low Power LoRa Sensor
  Sends BME280 data (temperature, humidity, pressure) with timestamp
  Compatible with EoRa Pi Gateway
*/

#include <RadioLib.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <time.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

#define EoRa_PI_V1
#include "boards.h"

// Node Configuration
const String NODE_ID = "NODE01";  // Change for each sensor node
const unsigned long TRANSMIT_INTERVAL = 60000;  // Send data every 60 seconds
const unsigned long DUTY_CYCLE_SLEEP = 30000;   // Sleep 30 seconds between checks

// LoRa Parameters - MUST MATCH GATEWAY EXACTLY
uint8_t txPower = 22;
float radioFreq = 915.0;
#define LORA_PREAMBLE_LENGTH 512
#define LORA_SPREADING_FACTOR 7
#define LORA_BANDWIDTH 125.0
#define LORA_CODINGRATE 7

// BME280 Configuration
#define SEALEVELPRESSURE_HPA (1013.25)
Adafruit_BME280 bme; // I2C interface

SX1262 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);

// Timing variables
unsigned long lastTransmission = 0;
unsigned long lastWORCheck = 0;
bool wokeFromWOR = false;

// Time configuration
#define TZ "EST+5EDT,M3.2.0/2,M11.1.0/2"
const char * ntpServer1 = "pool.ntp.org";
const char * ntpServer2 = "time.nist.gov";

char strftime_buf[64];

String getCurrentDateTime() {
  struct tm *ti;
  time_t tnow = time(nullptr);
  ti = localtime(&tnow);
  strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d-%H:%M:%S", localtime(&tnow));
  String dtStamp = String(strftime_buf);
  return dtStamp;
}

// Read BME280 sensor data
struct BME280Reading {
  float temperature_c;
  float temperature_f;
  float humidity;
  float pressure_hpa;
  float altitude_m;
  bool valid;
};

BME280Reading readBME280() {
  BME280Reading reading;
  reading.valid = false;
  
  if (!bme.begin(0x76)) {  // Try primary I2C address
    if (!bme.begin(0x77)) {  // Try alternate I2C address
      Serial.println("Could not find a valid BME280 sensor!");
      return reading;
    }
  }
  
  reading.temperature_c = bme.readTemperature();
  reading.temperature_f = (reading.temperature_c * 9.0/5.0) + 32.0;
  reading.humidity = bme.readHumidity();
  reading.pressure_hpa = bme.readPressure() / 100.0F;  // Convert Pa to hPa
  reading.altitude_m = bme.readAltitude(SEALEVELPRESSURE_HPA);
  reading.valid = true;
  
  return reading;
}

// Send BME280 data to gateway
void transmitSensorData() {
  BME280Reading sensor = readBME280();
  
  if (!sensor.valid) {
    Serial.println("❌ Failed to read BME280 sensor");
    return;
  }
  
  String timestamp = getCurrentDateTime();
  
  // Format: "NodeID,timestamp,temp_c,humidity,pressure_hpa"
  String payload = NODE_ID + "," + timestamp + "," + 
                   String(sensor.temperature_c, 2) + "," +
                   String(sensor.humidity, 1) + "," +
                   String(sensor.pressure_hpa, 2);
  
  Serial.println("\n🌡️  === TRANSMITTING BME280 DATA ===");
  Serial.printf("Node ID: %s\n", NODE_ID.c_str());
  Serial.printf("Timestamp: %s\n", timestamp.c_str());
  Serial.printf("Temperature: %.2f°C (%.1f°F)\n", sensor.temperature_c, sensor.temperature_f);
  Serial.printf("Humidity: %.1f%%\n", sensor.humidity);
  Serial.printf("Pressure: %.2f hPa\n", sensor.pressure_hpa);
  Serial.printf("Altitude: %.1f m\n", sensor.altitude_m);
  Serial.printf("Payload: %s\n", payload.c_str());
  Serial.printf("Payload Length: %d bytes\n", payload.length());
  
  // Transmit the data
  unsigned long startTime = millis();
  int state = radio.transmit(payload);
  unsigned long transmitTime = millis() - startTime;
  
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("✅ Sensor data transmitted successfully!");
    Serial.printf("Transmission time: %lu ms\n", transmitTime);
    Serial.printf("Data rate: %.2f bps\n", (payload.length() * 8.0) / (transmitTime / 1000.0));
  } else {
    Serial.printf("❌ Transmission failed! Error code: %d\n", state);
    
    switch(state) {
      case RADIOLIB_ERR_PACKET_TOO_LONG:
        Serial.println("Error: Packet too long");
        break;
      case RADIOLIB_ERR_TX_TIMEOUT:
        Serial.println("Error: Transmission timeout");
        break;
      case RADIOLIB_ERR_CHIP_NOT_FOUND:
        Serial.println("Error: Radio chip not responding");
        break;
      default:
        Serial.println("Check RadioLib documentation for error code details");
    }
  }
  
  Serial.println("=== TRANSMISSION COMPLETE ===\n");
}

// Check for incoming WOR or data requests
bool checkForCommands() {
  String receivedData;
  int state = radio.startReceive();
  
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("Failed to start receive: %d\n", state);
    return false;
  }
  
  // Wait briefly for incoming messages
  delay(1000);
  
  state = radio.readData(receivedData);
  
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("\n📡 === COMMAND RECEIVED ===");
    Serial.printf("RSSI: %.2f dBm\n", radio.getRSSI());
    Serial.printf("SNR: %.2f dB\n", radio.getSNR());
    Serial.printf("Command: %s\n", receivedData.c_str());
    
    // Check for Wake-on-Radio signal
    if (receivedData.indexOf("WOR") >= 0) {
      Serial.println("🚨 Wake-on-Radio signal detected!");
      wokeFromWOR = true;
      return true;
    }
    
    // Check for data request
    if (receivedData.startsWith("REQUEST," + NODE_ID)) {
      Serial.println("📊 Data request received for this node!");
      return true;
    }
    
    Serial.println("Command not for this node");
    Serial.println("=== END COMMAND ===\n");
  } else if (state != RADIOLIB_ERR_RX_TIMEOUT) {
    Serial.printf("Receive error: %d\n", state);
  }
  
  return false;
}

void setup() {
  Serial.begin(115200);
  while(!Serial){};
  
  Serial.println("=== BME280 LoRa Sensor Node Starting ===");
  Serial.printf("Node ID: %s\n", NODE_ID.c_str());
  
  // WiFi setup for time sync only
  WiFiManager wm;
  bool res = wm.autoConnect((NODE_ID + "_Setup").c_str(), "password");
  
  if (!res) {
    Serial.println("Failed to connect to WiFi - continuing without time sync");
  } else {
    Serial.println("WiFi connected - syncing time...");
    configTime(0, 0, ntpServer1, ntpServer2);
    setenv("TZ", TZ, 3);
    tzset();
    
    Serial.print("Waiting for NTP sync");
    while (time(nullptr) < 100000ul) {
      Serial.print(".");
      delay(1000);
    }
    Serial.println("\nTime synchronized!");
  }
  
  // Initialize hardware
  initBoard();
  
  // Initialize BME280
  Wire.begin();
  if (!bme.begin(0x76) && !bme.begin(0x77)) {
    Serial.println("❌ Could not find BME280 sensor!");
    Serial.println("Check wiring and I2C address");
  } else {
    Serial.println("✅ BME280 sensor initialized");
  }
  
  // Initialize LoRa radio
  Serial.println("=== CONFIGURING LoRa SENSOR NODE ===");
  
  int state = radio.begin(
    radioFreq,                          
    LORA_BANDWIDTH,                     
    LORA_SPREADING_FACTOR,              
    LORA_CODINGRATE,                    
    RADIOLIB_SX126X_SYNC_WORD_PRIVATE,