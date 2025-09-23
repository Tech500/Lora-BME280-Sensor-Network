/*
  LoRa BME280 Sensor Network - Real BME280 Integration
  Original concept and development: William Lucid
  AI development assistance: Claude (Anthropic) 
  
  Part of the open source LoRa BME280 Network project
  https://github.com/tech500/lora-bme280-sensor-network
  
  Hardware: EoRa-S3-900TB from EbyeIoT.com 
  BME280 Library: Tyler Glenn's BME280 library
  
  MIT License - See LICENSE file for details
*/

/*
  LoRa BME280 Sensor Network - Real BME280 Integration
  Original concept and development: William Lucid
  AI development assistance: Claude (Anthropic) 
  
  Part of the open source LoRa BME280 Network project
  https://github.com/tech500/lora-bme280-sensor-network
  
  Hardware: EoRa-S3-900TB from EbyeIoT.com 
  BME280 Library: Tyler Glenn's BME280 library
  
  MIT License - See LICENSE file for details
*/
//#define RADIOLIB_DEBUG

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

// BME280 Library by Tyler Glenn
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

// BME280 I2C pins for EoRa-S3-900TB
#define BME280_SDA_PIN 47  // Pin 20 - GPIO47 SDA
#define BME280_SCL_PIN 48  // Pin 19 - GPIO48 SCL

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
#define LORA_PREAMBLE_LENGTH symbols                      // INCREASED TO MATCH TRANSMITTER!
#define LORA_SYNC_WORD RADIOLIB_SX126X_SYNC_WORD_PRIVATE  // LoRa sync word

#define DUTY_CYCLE_PARAMS 100, 4900  // Test 5 --(Best --2% Duty cycle  100, 4900)

// ===== PACKET STRUCTURES =====
struct CommandPacket {
  uint8_t packetType;        // 0x01 for command
  uint16_t nodeID;           // target node (0xFFFF = broadcast)
  uint8_t sequenceNumber;    // command sequence
  char timestamp[32];        // command timestamp
  uint8_t checksum;
};

struct DataPacket {
  uint8_t packetType;        // 0x02 for sensor data
  uint16_t nodeID;           // responding node
  uint8_t sequenceNumber;    // matching command sequence
  char commandTimestamp[32]; // echo original command timestamp
  float temperature;         // BME280 reading
  float humidity;
  float pressure;
  float batteryVoltage;
  int16_t rssi;             // RSSI of received command
  uint8_t checksum;
};

// ===== NODE CONFIGURATION =====
uint16_t myNodeID = 0x1001;  // Unique node identifier
CommandPacket lastCommand;   // Store received command
bool commandReceived = false;

// ===== BME280 SENSOR =====
BME280I2C bme;    // Default: forced mode, standby time = 1000 ms, Oversampling = pressure ×1, temperature ×1, humidity ×1, filter off
bool bmeFound = false;

// Sensor reading variables
float currentTemp = 0.0;
float currentHumidity = 0.0;
float currentPressure = 0.0;

// Global variables
int task = 0;
String str;
String timestamp = "";

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
  for(size_t i = 0; i < length-1; i++) {
    sum ^= data[i];
  }
  return sum;
}

bool validateChecksum(uint8_t* data, size_t length) {
  return calculateChecksum(data, length) == data[length-1];
}

// ===== CONFIGURATION OPTIONS =====
// Set behavior when BME280 fails - choose ONE:
#define BME280_FAIL_BEHAVIOR_HALT      // Stop execution completely 
// #define BME280_FAIL_BEHAVIOR_DUMMY    // Use dummy values (for testing)
// #define BME280_FAIL_BEHAVIOR_SKIP     // Skip sensor readings entirely

// ===== BME280 SENSOR FUNCTIONS =====
bool initBME280() {
  Serial.println("🌡️ Initializing BME280 sensor...");
  
  // Initialize I2C with custom pins
  Wire.begin(BME280_SDA_PIN, BME280_SCL_PIN);
  
  // Scan I2C bus first
  Serial.println("📡 Scanning I2C bus...");
  int devicesFound = 0;
  for (byte address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.printf("   Device found at 0x%02X\n", address);
      devicesFound++;
    }
  }
  
  if (devicesFound == 0) {
    Serial.println("❌ No I2C devices found! Check wiring.");
  } else {
    Serial.printf("   Total devices found: %d\n", devicesFound);
  }
  
  // Try both common BME280 addresses
  Serial.println("🔍 Attempting BME280 initialization...");
  
  // Try address 0x76 first (default)
  bmeFound = bme.begin();
  
  if (!bmeFound) {
    Serial.println("   0x76 failed, trying 0x77...");
    BME280::Settings settings(
      BME280::OSR_X1, BME280::OSR_X1, BME280::OSR_X1,
      BME280::Mode_Forced, BME280::StandbyTime_1000ms,
      BME280::Filter_Off, BME280::SpiEnable_False,
      BME280::I2CAddr_0x77  // Try alternate address
    );
    
    BME280I2C bme_alt(settings);
    bme = bme_alt;
    bmeFound = bme.begin();
  }
  
  if (bmeFound) {
    Serial.println("✅ BME280 sensor found and initialized");
    
    // Print chip information
    switch(bme.chipModel()) {
      case BME280::ChipModel_BME280:
        Serial.println("   Chip: BME280 (Temperature, Humidity, Pressure)");
        break;
      case BME280::ChipModel_BMP280:
        Serial.println("   Chip: BMP280 (Temperature, Pressure only)");
        break;
      default:
        Serial.println("   Chip: Unknown BME280 variant");
    }
    
    // Configure BME280 for low power operation
    BME280::Settings settings(
      BME280::OSR_X1,    // Temperature oversampling x1
      BME280::OSR_X1,    // Humidity oversampling x1  
      BME280::OSR_X1,    // Pressure oversampling x1
      BME280::Mode_Forced, // Forced mode (one-shot)
      BME280::StandbyTime_1000ms,
      BME280::Filter_Off,
      BME280::SpiEnable_False,
      BME280::I2CAddr_0x76 // Use detected address
    );
    
    bme.setSettings(settings);
    
    // Test a reading to verify communication
    bme.setMode(BME280::Mode_Forced);
    delay(10);
    float testTemp, testHum, testPres;
    BME280::TempUnit tempUnit(BME280::TempUnit_Celsius);
    BME280::PresUnit presUnit(BME280::PresUnit_hPa);
    bme.read(testPres, testTemp, testHum, tempUnit, presUnit);
    
    if (testTemp > -50 && testTemp < 100) {
      Serial.printf("   Test reading successful: %.1f°C\n", testTemp);
      return true;
    } else {
      Serial.println("❌ BME280 communication failed on test reading");
      bmeFound = false;
    }
  }
  
  // Handle sensor failure based on configuration
  Serial.println("❌ BME280 SENSOR FAILURE!");
  Serial.println("   Possible causes:");
  Serial.println("   • Incorrect wiring (SDA/SCL swapped?)");
  Serial.println("   • Wrong I2C address (try 0x76 or 0x77)");
  Serial.println("   • Power supply issues");
  Serial.println("   • Faulty BME280 module");
  Serial.printf("   • Check pins: SDA=%d, SCL=%d\n", BME280_SDA_PIN, BME280_SCL_PIN);
  
#ifdef BME280_FAIL_BEHAVIOR_HALT
  Serial.println("🛑 HALTING EXECUTION - Fix BME280 before proceeding!");
  Serial.println("   (Change BME280_FAIL_BEHAVIOR to continue without sensor)");
  while(1) {
    digitalWrite(BOARD_LED, HIGH);
    delay(200);
    digitalWrite(BOARD_LED, LOW);
    delay(200);
  }
#elif defined(BME280_FAIL_BEHAVIOR_DUMMY)
  Serial.println("⚠️ CONTINUING WITH DUMMY VALUES - This is for testing only!");
  return false;
#elif defined(BME280_FAIL_BEHAVIOR_SKIP)
  Serial.println("⚠️ SENSOR READINGS WILL BE SKIPPED");
  return false;
#endif
  
  return false;
}

void readBME280() {
  if (!bmeFound) {
#ifdef BME280_FAIL_BEHAVIOR_DUMMY
    // Only use dummy values if explicitly configured
    Serial.println("📊 Using dummy sensor values (BME280 not available)");
    currentTemp = 22.5 + random(-10, 10) / 10.0;
    currentHumidity = 65.0 + random(-50, 50) / 10.0;
    currentPressure = 1013.2 + random(-10, 10) / 10.0;
    
    // Keep values in reasonable ranges
    currentTemp = constrain(currentTemp, 15.0, 35.0);
    currentHumidity = constrain(currentHumidity, 30.0, 95.0);
    currentPressure = constrain(currentPressure, 980.0, 1050.0);
#elif defined(BME280_FAIL_BEHAVIOR_SKIP)
    Serial.println("⚠️ Skipping sensor reading - BME280 not available");
    currentTemp = NAN;
    currentHumidity = NAN;
    currentPressure = NAN;
#else
    Serial.println("❌ Cannot read sensor - BME280 failed initialization");
    currentTemp = NAN;
    currentHumidity = NAN;
    currentPressure = NAN;
#endif
    return;
  }
  
  // Force a reading from BME280
  bme.setMode(BME280::Mode_Forced);
  
  // Wait for measurement to complete
  delay(10);
  
  // Read all values
  BME280::TempUnit tempUnit(BME280::TempUnit_Celsius);
  BME280::PresUnit presUnit(BME280::PresUnit_hPa);
  
  bme.read(currentPressure, currentTemp, currentHumidity, tempUnit, presUnit);
  
  Serial.printf("🌡️ BME280 readings: %.1f°C, %.1f%%, %.1fhPa\n", 
                currentTemp, currentHumidity, currentPressure);
  
  // Validate readings (BME280 should return reasonable values)
  bool readingValid = true;
  
  if (currentTemp < -40.0 || currentTemp > 85.0) {
    Serial.println("❌ Temperature reading out of range!");
    readingValid = false;
  }
  if (currentHumidity < 0.0 || currentHumidity > 100.0) {
    Serial.println("❌ Humidity reading out of range!");  
    readingValid = false;
  }
  if (currentPressure < 300.0 || currentPressure > 1100.0) {
    Serial.println("❌ Pressure reading out of range!");
    readingValid = false;
  }
  
  if (!readingValid) {
    Serial.println("⚠️ Invalid sensor readings detected - possible hardware issue");
    // Set to NaN to indicate invalid data
    currentTemp = NAN;
    currentHumidity = NAN;
    currentPressure = NAN;
  }
}

void setupLoRa(){
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
    512,   // Preamble length
    0.0,  // No TCXO
    true  // LDO mode ON
  );
    if (state == RADIOLIB_ERR_NONE)
    {
        Serial.println(F("success!"));
    }
    else
    {
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
    if (state == RADIOLIB_ERR_NONE)
    {
        Serial.println(F("success!\n"));
    }
    else
    {
        Serial.print(F("failed, code "));
        Serial.println(state);
        while (true)
            ;
    }
}

// ===== SENSOR DATA COLLECTION =====
void collectSensorData() {
  Serial.println("📊 Collecting sensor data...");
  readBME280();
}

void sendSensorResponse() {
  float batteryVoltage = 3.72;         // Stub value
  uint8_t packetType = 0x01;           // Sensor packet type

  static uint16_t sequenceNumber = 0;  // Sequence tracker
  sequenceNumber++;

  // Check if we have valid sensor data
  bool hasValidData = !isnan(currentTemp) && !isnan(currentHumidity) && !isnan(currentPressure);
  
  if (!hasValidData) {
    Serial.println("❌ No valid sensor data to transmit!");
#ifdef BME280_FAIL_BEHAVIOR_SKIP
    Serial.println("   Transmission skipped due to sensor failure");
    return;  // Don't send anything
#endif
  }

  // Format sensor values into strings
  char temperature[10];
  char humidity[10]; 
  char baroPressure[12];
  
  if (hasValidData) {
    dtostrf(currentTemp, 6, 1, temperature);
    dtostrf(currentHumidity, 6, 1, humidity); 
    dtostrf(currentPressure, 6, 3, baroPressure);
  } else {
    // Send error indicators
    strcpy(temperature, "ERROR");
    strcpy(humidity, "ERROR");  
    strcpy(baroPressure, "ERROR");
  }

  // Build response string
  String response = String(myNodeID, HEX) + "," + timestamp + ","
                  + temperature + "," + humidity + "," + baroPressure + " ,"
                  + String(batteryVoltage, 2) + "V";

  // Log packet info
  Serial.printf("📊 Sending packet: Type=0x%02X, Size=%d bytes\n",
                packetType, response.length());
  
  if (!hasValidData) {
    Serial.println("⚠️ WARNING: Transmitting ERROR values due to sensor failure");
  }

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

void taskDispatcher() {
  // Wake over radio
  digitalWrite(BUILTIN_LED, HIGH);
  Serial.println("✅ WOR");
  Serial.println(timestamp);
  setupLoRa();
  radio.startReceive();   //radio.startReceiveDutyCycleAuto();
  collectSensorData();  //BME280 data
  delay(250);
  sendSensorResponse();  //Send to Gateway
  radio.sleep();
  wakeByTimer();
  Serial.println("✅ Timer Expired");
  setupLoRa();  //Back to Duty Cycle listening
  digitalWrite(BUILTIN_LED, LOW);
  goToSleep();
}

//No parsing required!

void wakeByTimer(){
  // Set deep sleep timer for 30s test / 120s production
  esp_sleep_enable_timer_wakeup(30 * 1000000ULL); // 30 seconds in microseconds
  // or
  //esp_sleep_enable_timer_wakeup(120 * 1000000ULL); // 120 seconds

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

  Serial.println("✅ Going to deep sleep now...");
  Serial.println("Wake-up sources: DIO1 pin reroute\n");
  Serial.flush();  // Make sure all serial data is sent before sleep

  SPI.end();  //Power saving

  // Finally set ESP32 into sleep
  esp_deep_sleep_start();  
}

void setup() {
  setCpuFrequencyMhz(80);
  Serial.begin(115200);
  Serial.println("LoRa Network Node Starting...");
  Serial.printf("Node ID: %04X\n", myNodeID);

  bool fsok = LittleFS.begin(true);
  Serial.printf_P(PSTR("\nFS init: %s\n"), fsok ? PSTR("ok") : PSTR("fail!"));

  SPI.begin(RADIO_SCLK_PIN, RADIO_MISO_PIN, RADIO_MOSI_PIN);
  
  Serial.print("CPU Frequency: ");
  Serial.print(getCpuFrequencyMhz());
  Serial.println(" MHz");
  
  // Power management optimizations
  eora_disable_wifi();
  eora_disable_bluetooth();
  eora_disable_adc();

  // Initialize BME280 sensor
  initBME280();

  // Check if this is a power-on reset
  if (esp_reset_reason() == ESP_RST_POWERON) {
    printf("Power-on reset detected - initializing...\n");
    setupLoRa();
    goToSleep(); // Conserve power until Preamble arrives
  }

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER){
    Serial.println("⏰ Woke from timer - sensor reading time");
    setupLoRa();
    goToSleep();
  }
  
  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println("📡 Woke from LoRa packet");
    setupLoRa();
    taskDispatcher();
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
  bool stopRepeating = true;

  Serial.println("Entering loop");
  stopRepeating = false;  //Resets on wake up
    
  //check if the flag is set
    if (receivedFlag)
    {
        // disable the interrupt service routine while
        // processing the data
        enableInterrupt = false;

        // reset flag
        receivedFlag = false;

        // you can read received data as an Arduino String
        String str;
        int state = radio.readData(str);

        // you can also read received data as byte array
        /*
          byte byteArr[8];
          int state = radio.readData(byteArr, 8);
        */

        if (state == RADIOLIB_ERR_NONE)
        {
            // packet was successfully received
            Serial.println(F("[SX126x] Received packet!"));

            // print data of the packet
            Serial.print(F("[SX126x] Data:\t\t"));
            Serial.println(str);
            //parseString(str);
            //taskDispatcher(task, timestamp);

            // print RSSI (Received Signal Strength Indicator)
            Serial.print(F("[SX126x] RSSI:\t\t"));
            Serial.print(radio.getRSSI());
            Serial.println(F(" dBm"));

            // print SNR (Signal-to-Noise Ratio)
            Serial.print(F("[SX126x] SNR:\t\t"));
            Serial.print(radio.getSNR());
            Serial.println(F(" dB"));
        }
        else if (state == RADIOLIB_ERR_CRC_MISMATCH)
        {
            // packet was received, but is malformed
            Serial.println(F("CRC error!"));
        }
        else
        {
            // some other error occurred
            Serial.print(F("failed, code "));
            Serial.println(state);
        }

        // put module back to listen mode
        //radio.startReceive();  //example
        // Set up the radio for duty cycle receiving
        //radio.startReceiveDutyCycleAuto();
        radio.startReceiveDutyCycleAuto();

        // we're ready to receive more packets,
        // enable interrupt service routine
        enableInterrupt = true;
    }
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
