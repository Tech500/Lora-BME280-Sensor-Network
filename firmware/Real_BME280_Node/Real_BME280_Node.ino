/*
  LoRa BME280 Sensor Network
  Original concept and development: William Lucid
  AI development assistance: Claude (Anthropic) 
  
  Part of the open source LoRa BME280 Network project
  https://github.com/tech500/lora-bme280-sensor-network
  
  Hardware: EoRa-S3-900TB from EbyeIoT.com 
  
  MIT License - See LICENSE file for details
*/


#define RADIOLIB_DEBUG

#include <RadioLib.h>
#define EoRa_PI_V1

#include "boards.h"
#include <Wire.h>
#include <SPI.h>
#include <FS.h>
#include <LittleFS.h>
#include <rom/rtc.h>
#include <driver/rtc_io.h>
#include <esp_system.h>
#include "eora_s3_power_mgmt.h"
#include <BME280I2C.h>

// "EoRa Pi" development board pin assignments
#define RADIO_SCLK_PIN 5
#define RADIO_MISO_PIN 3
#define RADIO_MOSI_PIN 6
#define RADIO_CS_PIN 7
#define RADIO_DIO1_PIN 33  //Not RTC_IO Wakecapable! --Must be listed here!
#define RADIO_BUSY_PIN 34
#define RADIO_RST_PIN 8
#define BOARD_LED 37

// RADIO_DIO1_PIN 33  is not External wake capable!
#define WAKE_PIN GPIO_NUM_16  // No exceptions! -DIO1 signal re-routed!

#define BOARD_LED 37
/** LED on level */
#define LED_ON HIGH
/** LED off level */
#define LED_OFF LOW

// === "EoRa PI" development board LoRa configuration ==================
#define USING_SX1262_868M

#if defined(USING_SX1268_433M)
uint8_t txPower = 14;
float radioFreq = 433.0;
SX1268 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);
#elif defined(USING_SX1262_868M)
uint8_t txPower = 14;
float radioFreq = 915.0;
SX1262 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);
#endif

SPIClass spi(SPI);
SPISettings spiSettings(2000000, MSBFIRST, SPI_MODE0);

// Define LoRa parameters (MUST MATCH TRANSMITTER EXACTLY!)
#define RF_FREQUENCY 915.0                                // MHz
#define TX_OUTPUT_POWER 14                                // dBm
#define LORA_BANDWIDTH 125.0                              // kHz
#define LORA_SPREADING_FACTOR 7                           // [SF7..SF12]
#define LORA_CODINGRATE 7                                 // [1: 4/5, 2: 4/6, 3: 4/7, 4: 4/8] -> RadioLib uses 7 for 4/7
#define LORA_PREAMBLE_LENGTH 512                      // INCREASED TO MATCH TRANSMITTER!
#define LORA_SYNC_WORD RADIOLIB_SX126X_SYNC_WORD_PRIVATE  // LoRa sync word

#define DUTY_CYCLE_PARAMS  //No parameters

// ===== PACKET STRUCTURES =====
struct CommandPacket {
  uint8_t packetType;      // 0x01 for command
  uint16_t nodeID;         // target node (0xFFFF = broadcast)
  uint8_t sequenceNumber;  // command sequence
  String   lastUpdate;      // command timestamp
  uint8_t checksum;
};

struct DataPacket {
  uint8_t packetType;         // 0x02 for sensor data
  uint16_t nodeID;            // responding node
  uint8_t sequenceNumber;     // matching command sequence
  String lastUpdate;
  float temperature;          // BME280 simulation
  float humidity;
  float pressure;
  float batteryVoltage;
  int16_t rssi;  // RSSI of received command
  uint8_t checksum;
};

// ===== NODE CONFIGURATION =====
uint16_t myNodeID = 0x1001;  // Unique node identifier
CommandPacket lastCommand;   // Store received command
bool commandReceived = false;

 bool wokeFromWOR = false;
 String storedTimestamp = "";
 
// Global variables
int task = 0;
String str;
String timestamp;

String centigrade, humidity, pressure;

#define BUFFER_SIZE 512  // Define the payload size here

// flag to indicate that a packet was received
volatile bool receivedFlag = false;

// disable interrupt when it's not needed
volatile bool enableInterrupt = true;

// this function is called when a complete packet
// is received by the module
// IMPORTANT: this function MUST be 'void' type
//            and MUST NOT have any arguments!
void setFlag(void) {
  // check if the interrupt is enabled
  if (!enableInterrupt) {
    return;
  }

  // we got a packet, set the flag
  receivedFlag = true;
}

// Debug: Track wake-up source
String wakeupReason = "Unknown";

/** Print reset reason */
void print_reset_reason(RESET_REASON reason);

// ===== CHECKSUM CALCULATION =====
uint8_t calculateChecksum(uint8_t* data, size_t length) {
  uint8_t sum = 0;
  for (size_t i = 0; i < length - 1; i++) {
    sum ^= data[i];
  }
  return sum;
}

bool validateChecksum(uint8_t* data, size_t length) {
  return calculateChecksum(data, length) == data[length - 1];
}

void setupLoRa() {
  initBoard();
  //Delay is required.
  delay(1500);

  // initialize SX126x with default settings
  Serial.print(F("[SX126x] Initializing ... "));
  //int state = radio.begin(radioFreq);  //example
  int state = radio.begin(
    radioFreq,  // 915.0 MHz
    125.0,      // Bandwidth
    7,          // Spreading factor
    7,          // Coding rate
    RADIOLIB_SX126X_SYNC_WORD_PRIVATE,
    14,   // 14 dBm for good balance
    512,  // Preamble length
    0.0,  // No TCXO
    true  // LDO mode ON
  );
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println(F("success!"));
  } else {
    Serial.print(F("failed, code "));
    Serial.println(state);
    while (true)
      ;
  }

  // set the function that will be called
  // when new packet is received
  radio.setDio1Action(setFlag);

  // start listening for LoRa packets
  Serial.print(F("[SX126x] Starting to listen ... "));
  //state = radio.startReceive();  //example
  // Set up the radio for duty cycle receiving
  state = radio.startReceiveDutyCycleAuto();
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println(F("success!\n"));
  } else {
    Serial.print(F("failed, code "));
    Serial.println(state);
    while (true)
      ;
  }
}

// BME280 I2C pins for EoRa-S3-900TB
#define I2C_SDA 47
#define I2C_SCL 48 

//BME280 Settings
BME280I2C::Settings settings(
  BME280::OSR_X1,
  BME280::OSR_X1,
  BME280::OSR_X1,
  BME280::Mode_Forced,
  BME280::StandbyTime_1000ms,
  BME280::Filter_Off,
  BME280::SpiEnable_False,
  BME280I2C::I2CAddr_0x76
);

BME280I2C bme(settings);

// ===== SENSOR VALUES =====
float temperatureC;
float humidityPercent;
float pressureHPa;

void collectSensorData() {
  float temp(NAN), hum(NAN), pres(NAN);
  BME280::TempUnit tempUnit(BME280::TempUnit_Celsius);
  BME280::PresUnit presUnit(BME280::PresUnit_Pa);

  bme.read(pres, temp, hum, tempUnit, presUnit);

  temperatureC = temp;
  humidityPercent = hum;
  pressureHPa = pres / 100.0;  // Convert Pa to hPa

  Serial.printf("📊 BME280 readings: %.1f°C, %.1f%%, %.1f hPa\n",
                temperatureC, humidityPercent, pressureHPa);
}

void sendSensorResponse() {
  float batteryVoltage = 3.72;  // Stub value
  uint8_t packetType = 0x01;    // Sensor packet type

  static uint16_t sequenceNumber = 0;  // Sequence tracker
  sequenceNumber++;

  // Format sensor values into strings
  char temperature[7];
  dtostrf(temperatureC, 6, 1, temperature);

  char humidity[7];
  dtostrf(humidityPercent, 6, 1, humidity);

  char baroPressure[9];
  dtostrf(pressureHPa, 6, 3, baroPressure);

  // Build response string
  String response = String(myNodeID, HEX) + ","
                    + temperature + "," + humidity + "," + baroPressure + " ,"
                    + String(batteryVoltage, 2);

  // Log packet info
  Serial.printf("📊 Sending packet: Type=0x%02X, Size=%d bytes\n",
                packetType, response.length());

  // Collision avoidance delay
  delay(100 + (myNodeID & 0xFF) * 2);

  // Transmit response
  int state = radio.transmit((uint8_t*)response.c_str(), response.length());

  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("📤 Sensor response sent successfully");
    Serial.printf("   Node: %04X, Sequence: %d\n", myNodeID, sequenceNumber);
    Serial.printf("   Data: %s\n", response.c_str());
  } else {
    Serial.printf("❌ Response transmission failed, error code: %d\n", state);
  }
}

void taskDispatcher(String storedTimestamp) {
  // Wake over radio
  digitalWrite(BUILTIN_LED, HIGH);
  //collectSensorData();  //BME280 data
  sendSensorResponse();  //Send to Gateway
  Serial.print("   Time:  ");
  Serial.println(storedTimestamp);
  Serial.println("   Radio.sleep mode next 14 Min.");
  Serial.println("   ESP32-S3 going to sleep");
  radio.sleep();
  wakeByTimer();
  Serial.println("✅ Timer Expired");
  setupLoRa();  //Back to Duty Cycle listening
  digitalWrite(BUILTIN_LED, LOW);
  goToSleep();
}

void wakeByTimer() {
  // Set deep sleep timer for 14 Minutes production
  esp_sleep_enable_timer_wakeup(840 * 1000000ULL);  // 840 seconds in microseconds

  // Go to deep sleep - ESP32 wakes itself up after timer
  esp_deep_sleep_start();
}

void goToSleep(void) {
  Serial.println("=== PREPARING FOR DEEP SLEEP ===");
  Serial.printf("DIO1 pin state before sleep: %d\n", digitalRead(RADIO_DIO1_PIN));
  Serial.printf("Wake pin (GPIO16) state before sleep: %d\n", digitalRead(WAKE_PIN));

  // Set up the radio for duty cycle receiving
  radio.startReceiveDutyCycleAuto();

  Serial.println("Configuring RTC GPIO and deep sleep wake-up...");
  // Configure GPIO16 for RTC wake-up - using internal pull-down
  rtc_gpio_pulldown_en(WAKE_PIN);  // Internal pull-down on GPIO16

  // Setup deep sleep with wakeup by GPIO16 - RISING edge (buffered DIO1 signal)
  esp_sleep_enable_ext0_wakeup(WAKE_PIN, RISING);

  // Turn off LED before sleep
  digitalWrite(BOARD_LED, LED_OFF);

  Serial.println("\n✅ Going to deep sleep now...");
  Serial.println("Wake-up sources: DIO1 pin reroute\n");
  Serial.flush();  // Make sure all serial data is sent before sleep

  SPI.end();  //Power saving

  // Finally set ESP32 into sleep
  esp_deep_sleep_start();
}

void setup() {
  setCpuFrequencyMhz(80);
  Serial.begin(115200);
  Serial.println("LoRa Network Node  v0.8 Starting...");
  Serial.printf("Node ID: %04X\n", myNodeID);

  bool fsok = LittleFS.begin(true);
  Serial.printf_P(PSTR("\nFS init: %s\n"), fsok ? PSTR("ok") : PSTR("fail!"));

  Wire.begin(I2C_SDA, I2C_SCL);  // SDA = GPIO 47, SCL = GPIO 48
    delay(300);
    if (!bme.begin()) {
    Serial.println("❌ BME280 sensor not found!");
  } else {
    Serial.println("✅ BME280 sensor initialized");
  }

  SPI.begin(RADIO_SCLK_PIN, RADIO_MISO_PIN, RADIO_MOSI_PIN);

  Serial.print("CPU Frequency: ");
  Serial.print(getCpuFrequencyMhz());
  Serial.println(" MHz");

  // Power management optimizations
  eora_disable_wifi();
  eora_disable_bluetooth();
  eora_disable_adc();

  // Check if this is a power-on reset
  if (esp_reset_reason() == ESP_RST_POWERON) {
    printf("Power-on reset detected - initializing...\n");
    setupLoRa();
    goToSleep();
  }

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("⏰ Woke from timer - sensor reading time");
    setupLoRa();
    //checkFlag();
    goToSleep();
  }

  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println("✅ Woke from LoRa packet");
    setupLoRa();
    collectSensorData();
    delay(250);

    // First call should detect the WOR packet and set wokeFromWOR=true
    checkFlag();

    delay(250);

    // Second call should read the timestamp and set the global storedTimestamp
    checkFlag();

    // Dispatch only if the global storedTimestamp is non-empty
    if (storedTimestamp.length() > 0) {
      Serial.println("Dispatching with storedTimestamp: " + storedTimestamp);
      taskDispatcher(storedTimestamp);  // Now it's valid
      storedTimestamp = "";  // clear for next cycle
    } else {
      Serial.println("No timestamp found after two reads.");
    }
    taskDispatcher(storedTimestamp);
  }

  // 🔥 NEW: Check if this is a power-on reset (compile/upload/reset)
  if (wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED) {
    Serial.println("Power-on reset - initializing and going to sleep");
    setupLoRa();
    return;

    Serial.println("Going to sleep - wake on first packet");
    goToSleep();
  }

  // This should rarely execute now
  Serial.println("Unknown wake reason - staying awake");
}

void loop() {
}

void checkFlag() {
  // use the global wokeFromWOR and storedTimestamp
  if (!receivedFlag) return;

  receivedFlag = false;
  enableInterrupt = false;

  String str;
  int state = radio.readData(str);

  Serial.println("📦 Received string: " + str);
  Serial.println("📶 Radio state: " + String(state));

  if (state == RADIOLIB_ERR_NONE) {
    if (str.startsWith("WOR--")) {
      wokeFromWOR = true;
      Serial.println("✅ WOR packet received");
    } else if (wokeFromWOR) {
      // store into the global variable (not a local static)
      storedTimestamp = str;
      Serial.println("📅 Timestamp stored (global): " + storedTimestamp);
      Serial.flush();

      // Reset the temporary wake flag so next cycle is clean
      wokeFromWOR = false;
      // DO NOT call taskDispatcher() here — let setup() call it once
    } else {
      Serial.println("⚠️ Unexpected packet: " + str);
    }
  } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
    Serial.println("⚠️ CRC mismatch");
  } else {
    Serial.println("❌ Packet read failed, code: " + String(state));
  }

  // re-enable receiving and interrupts
  radio.startReceiveDutyCycleAuto();
  enableInterrupt = true;
}

void print_reset_reason(RESET_REASON reason) {
  switch (reason) {
    case 1:
      Serial.println("POWERON_RESET");
      break;
    case 3:
      Serial.println("SW_RESET");
      break;
    case 4:
      Serial.println("OWDT_RESET");
      break;
    case 5:
      Serial.println("DEEPSLEEP_RESET");
      break;
    case 6:
      Serial.println("SDIO_RESET");
      break;
    case 7:
      Serial.println("TG0WDT_SYS_RESET");
      break;
    case 8:
      Serial.println("TG1WDT_SYS_RESET");
      break;
    case 9:
      Serial.println("RTCWDT_SYS_RESET");
      break;
    case 10:
      Serial.println("INTRUSION_RESET");
      break;
    case 11:
      Serial.println("TGWDT_CPU_RESET");
      break;
    case 12:
      Serial.println("SW_CPU_RESET");
      break;
    case 13:
      Serial.println("RTCWDT_CPU_RESET");
      break;
    case 14:
      Serial.println("EXT_CPU_RESET");
      break;
    case 15:
      Serial.println("RTCWDT_BROWN_OUT_RESET");
      break;
    case 16:
      Serial.println("RTCWDT_RTC_RESET");
      break;
    default:
      Serial.println("NO_MEAN");
  }
}
