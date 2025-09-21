#include <OneWire.h>
#include <DallasTemperature.h>
#include <TinyGPS++.h>
#include <Adafruit_MLX90614.h>
#include <BluetoothSerial.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ===== LORA ADDRESSING =====
#define MY_ADDRESS 1
#define REPEATER_ADDRESS 2

// ===== PIN DEFINITIONS =====
#define DS18B20_PIN 4
#define EMERGENCY_BTN 15
#define BUZZER_PIN 25
#define HEATER_PIN 23   // MOSFET control untuk heating pad

#define GPS_RX 16
#define GPS_TX 17
#define GPS_BAUD 9600

#define LORA_RX 26
#define LORA_TX 27
#define LORA_BAUD 9600
#define LORA_M0 14
#define LORA_M1 12

#define LED_BUILTIN 2

// ===== SENSOR & SERIAL OBJECTS =====
OneWire oneWire(DS18B20_PIN);
DallasTemperature ds18b20(&oneWire);
Adafruit_MLX90614 mlx = Adafruit_MLX90614();
TinyGPSPlus gps;
BluetoothSerial BTSerial;

HardwareSerial loraSerial(1);
HardwareSerial gpsSerial(2);

// ===== TIMING & INTERVALS =====
const uint32_t LORA_SEND_INTERVAL = 5000;
const uint32_t LORA_PING_INTERVAL = 2000;
const uint32_t BT_SEND_INTERVAL_MS = 2000;

uint32_t lastLoraSend = 0;
uint32_t lastLoraPing = 0;
uint32_t lastBtSendTime = 0;
uint32_t lastAckTime = 0;

// ===== STATE & STATUS VARIABLES =====
enum HypoStatus { NORMAL, MILD, SEVERE };
HypoStatus currentStatus = NORMAL;

String NIK = "0"; // National Identification Number

// Sensor data
float bodyTemp = 37.0;
float ambientTemp = 25.0;

// Connection status
bool isConnect = false;

// Emergency & Heater status
bool emergencyTriggered = false;
int heaterPowerPercentage = 0;
int lastHeaterPowerPercentage = 50;
bool isHeaterOn = false;
uint32_t lastMildIgnoreTime = 0;
bool mildIgnored = false;
bool lastButtonState = HIGH;

// Buzzer task handle
TaskHandle_t buzzerTaskHandle = NULL;

// Recovery detection
const int RECOVERY_WINDOW_SECONDS = 20;
float tempHistory[RECOVERY_WINDOW_SECONDS];
int historyIndex = 0;
uint32_t lastHistoryUpdateTime = 0;


// =================================
//      BUZZER CONTROL FUNCTIONS
// =================================
void buzzerLoop(void *parameter) {
  int pattern = *((int*)parameter);
  while (1) {
    if (pattern == 1) { // MILD pattern: Beep every 2 seconds
      digitalWrite(BUZZER_PIN, HIGH);
      vTaskDelay(200 / portTICK_PERIOD_MS);
      digitalWrite(BUZZER_PIN, LOW);
      vTaskDelay(1800 / portTICK_PERIOD_MS);
    } else if (pattern == 2) { // SEVERE/EMERGENCY pattern: Fast beep
      digitalWrite(BUZZER_PIN, HIGH);
      vTaskDelay(100 / portTICK_PERIOD_MS);
      digitalWrite(BUZZER_PIN, LOW);
      vTaskDelay(100 / portTICK_PERIOD_MS);
    } else {
      digitalWrite(BUZZER_PIN, LOW);
      vTaskDelete(buzzerTaskHandle);
    }
  }
}

void startBuzzer(int pattern) {
  if (buzzerTaskHandle != NULL) {
    vTaskDelete(buzzerTaskHandle);
  }
  static int task_pattern;
  task_pattern = pattern;
  xTaskCreate(buzzerLoop, "BuzzerTask", 1000, &task_pattern, 1, &buzzerTaskHandle);
  Serial.printf("[BUZZER] Pattern %d activated.\n", pattern);
}

void stopBuzzer() {
  if (buzzerTaskHandle != NULL) {
    vTaskDelete(buzzerTaskHandle);
    buzzerTaskHandle = NULL;
  }
  digitalWrite(BUZZER_PIN, LOW);
  Serial.println("[BUZZER] Buzzer deactivated.");
}

// =================================
//      HEATER CONTROL FUNCTION
// =================================
void setHeaterPower(int powerPercent) {
  powerPercent = constrain(powerPercent, 0, 100);
  heaterPowerPercentage = powerPercent;

  if (powerPercent > 0) {
    isHeaterOn = true;
    lastHeaterPowerPercentage = powerPercent;
  } else {
    isHeaterOn = false;
  }

  int dutyCycle = map(heaterPowerPercentage, 0, 100, 0, 255);
  analogWrite(HEATER_PIN, dutyCycle);
  Serial.printf("[HEATER] Power set to %d%%. Status: %s.\n", heaterPowerPercentage, isHeaterOn ? "ON" : "OFF");
}

// =================================
//      DATA & TIME FUNCTIONS
// =================================
String buildLocalTime() {
  if (!gps.location.isValid() || gps.date.year() < 2020)
    return "0000-00-00 00:00:00";

  int year = gps.date.year();
  int month = gps.date.month();
  int day = gps.date.day();
  int hour = gps.time.hour();
  int minute = gps.time.minute();
  int second = gps.time.second();

  int offset = round(gps.location.lng() / 15.0);
  hour += offset;
  if (hour >= 24) hour -= 24;
  else if (hour < 0) hour += 24;

  char buf[30];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
           year, month, day, hour, minute, second);
  return String(buf);
}

const char* statusToString(HypoStatus status) {
  switch (status) {
    case NORMAL: return "NORMAL";
    case MILD:   return "MILD";
    case SEVERE: return "SEVERE";
    default:     return "UNKNOWN";
  }
}

// =================================
//      LORA COMMUNICATION
// =================================
void sendLoraPacket(bool emergency = false) {
  String payload;
  String header = String(REPEATER_ADDRESS) + "," + String(MY_ADDRESS) + ",";

  if (gps.location.isValid() && gps.date.year() >= 2020) {
    payload = String(gps.location.lat(), 6) + "," +
              String(gps.location.lng(), 6) + "," +
              String(ambientTemp, 2) + "," +
              String(bodyTemp, 2) + "," +
              buildLocalTime();
  } else {
    payload = "0,0," +
              String(ambientTemp, 2) + "," +
              String(bodyTemp, 2) + ",0000-00-00 00:00:00";
  }

  payload += "," + NIK; // Add NIK to payload

  if (emergency) payload += ",EMERGENCY";

  String fullMessage = header + payload + "\n";
  
  loraSerial.print(fullMessage);
  Serial.print("Kirim Data LORA: ");
  Serial.print(fullMessage);
}

void sendLoraAckPing() {
  String pingMessage = String(REPEATER_ADDRESS) + "," + String(MY_ADDRESS) + ",ACK," + NIK + "\n";
  loraSerial.print(pingMessage);
  Serial.println("[LORA] ACK/Ping dikirim ke Repeater");
}

void handleLoraIncoming() {
  static String buffer;
  while (loraSerial.available()) {
    char c = loraSerial.read();
    if (c == '\n') {
      int destAddr, srcAddr;
      char payload[20];
      sscanf(buffer.c_str(), "%d,%d,%19s", &destAddr, &srcAddr, payload);

      if (destAddr == MY_ADDRESS && String(payload) == "ACK") {
        lastAckTime = millis();
        Serial.printf("[LORA] ACK diterima dari Addr %d\n", srcAddr);
      }
      buffer = "";
    } else if (c != '\r') {
      buffer += c;
      if (buffer.length() > 50) buffer = "";
    }
  }
}

void updateLedStatus() {
  static uint32_t lastToggle = 0;
  static bool ledState = false;
  if (millis() - lastAckTime < 4000) { // Connected to repeater
    isConnect = true;
    if (millis() - lastToggle >= 1000) {
      ledState = !ledState;
      digitalWrite(LED_BUILTIN, ledState);
      lastToggle = millis();
    }
  } else { // Not connected
    isConnect = false;
    digitalWrite(LED_BUILTIN, LOW);
  }
}

// =================================
//      BLUETOOTH & SERIAL INPUT
// =================================
void sendDataToApp() {
  String json = "{\n";
  json += "  \"bodyTemp\": " + String(bodyTemp, 2) + ",\n";
  json += "  \"envTemp\": " + String(ambientTemp, 2) + ",\n";
  json += "  \"heaterPower\": " + String(heaterPowerPercentage) + ",\n";
  json += "  \"isHeaterOn\": " + String(isHeaterOn ? "true" : "false") + ",\n";
  if (gps.location.isValid()) {
    json += "  \"gps\": { \"lat\": " + String(gps.location.lat(), 6) + ", \"lon\": " + String(gps.location.lng(), 6) + " },\n";
  } else {
    json += "  \"gps\": { \"lat\": 0, \"lon\": 0 },\n";
  }
  json += "  \"hypoStatus\": \"" + String(statusToString(currentStatus)) + "\",\n";
  json += "  \"isConnect\": " + String(isConnect ? "true" : "false") + ",\n";
  json += "  \"nik\": \"" + NIK + "\",\n";
  json += "  \"emergency\": " + String(emergencyTriggered ? "true" : "false") + "\n";
  json += "}";

  BTSerial.println(json);
  emergencyTriggered = false; // Reset after sending
}

void handleAppInput() {
  if (BTSerial.available() > 0) {
    String input = BTSerial.readStringUntil('\n');
    input.trim();
    input.toUpperCase();
    Serial.printf("[BT_IN] Diterima: %s\n", input.c_str());

    if (input.startsWith("NIK:")) {
      NIK = input.substring(4);
      NIK.trim();
      BTSerial.println("{\"status\":\"OK\",\"message\":\"NIK set to " + NIK + "\"}");
      Serial.printf("[BT_IN] NIK diperbarui: %s\n", NIK.c_str());
      return;
    }

    if (input.startsWith("E")) {
      emergencyTriggered = true;
      Serial.println("[ALERT] Tombol darurat diaktifkan via App!");
      sendLoraPacket(true);
      currentStatus = SEVERE; // Force SEVERE state
      checkHypothermiaStatus(); // Trigger immediate action for SEVERE
      return;
    }
    
    if (input == "IGNORE_MILD") {
        if (currentStatus == MILD) {
            stopBuzzer();
            mildIgnored = true;
            lastMildIgnoreTime = millis();
            BTSerial.println("{\"status\":\"OK\",\"message\":\"Peringatan MILD diabaikan.\"}");
        } else {
            BTSerial.println("{\"status\":\"ERROR\",\"message\":\"Tidak ada peringatan MILD yang aktif.\"}");
        }
        return;
    }

    if (input == "ACTIVATE_HEATER") {
        if (currentStatus == MILD) {
            setHeaterPower(lastHeaterPowerPercentage);
            stopBuzzer();
            mildIgnored = false;
            BTSerial.println("{\"status\":\"OK\",\"message\":\"Pemanas diaktifkan sesuai permintaan.\"}");
        } else {
            BTSerial.println("{\"status\":\"ERROR\",\"message\":\"Perintah hanya berlaku pada status MILD.\"}");
        }
        return;
    }

    if (input == "HEAT_ON") {
      if (currentStatus != SEVERE) {
        setHeaterPower(lastHeaterPowerPercentage);
        BTSerial.println("{\"status\":\"OK\",\"message\":\"Heater ON to " + String(lastHeaterPowerPercentage) + "%%\"}");
      } else {
        BTSerial.println("{\"status\":\"ERROR\",\"message\":\"Manual control denied in SEVERE state.\"}");
      }
      return;
    }

    if (input == "HEAT_OFF") {
      if (currentStatus != SEVERE) {
        setHeaterPower(0);
        BTSerial.println("{\"status\":\"OK\",\"message\":\"Heater OFF\"}");
      } else {
        BTSerial.println("{\"status\":\"ERROR\",\"message\":\"Manual control denied in SEVERE state.\"}");
      }
      return;
    }

    if (input == "RESET") {
      BTSerial.println("{\"status\":\"OK\",\"message\":\"ESP32 restarting...\"}");
      Serial.println("[BT_IN] RESET command received. Restarting...");
      delay(1000); // Give BT serial time to send the message
      ESP.restart();
      return;
    }
    
    if (input.startsWith("P")) {
      if (currentStatus != SEVERE) {
        int powerValue = input.substring(1).toInt();
        setHeaterPower(powerValue);
        BTSerial.println("{\"status\":\"OK\",\"message\":\"Heater power set to " + String(powerValue) + "%%\"}");
      } else {
        BTSerial.println("{\"status\":\"ERROR\",\"message\":\"Manual control denied in SEVERE state.\"}");
      }
      return;
    }
  }
}

void handleSerialMonitorInput() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    Serial.printf("[SERIAL_IN] Diterima: %s\n", input.c_str());

    input.toUpperCase();
    if (input.startsWith("NIK:")) {
      NIK = input.substring(4);
      NIK.trim();
      Serial.printf("[SERIAL_IN] NIK diperbarui: %s\n", NIK.c_str());
    }
  }
}

// =================================
//      CORE LOGIC & STATUS CHECKS
// =================================

void checkRecovery() {
  int aboveCount = 0;
  for (int i = 0; i < RECOVERY_WINDOW_SECONDS; i++) {
    if (tempHistory[i] > 32.0) {
      aboveCount++;
    }
  }

  if (aboveCount > RECOVERY_WINDOW_SECONDS / 2) {
    Serial.println("[RECOVERY] Kondisi membaik. Kembali ke status MILD.");
    currentStatus = MILD;
    setHeaterPower(0);
    startBuzzer(1);
    BTSerial.println("{\"status\":\"WARNING\",\"warning_type\":\"MILD\",\"message\":\"Kondisi membaik, pemanas dinonaktifkan. Suhu masih rendah.\"}");
  }
}

void checkHypothermiaStatus() {
  HypoStatus lastStatus = currentStatus;

  // Determine current status based on body temperature
  if (bodyTemp < 32.0) {
    currentStatus = SEVERE;
  } else if (bodyTemp < 36.0) {
    if (currentStatus != SEVERE) {
       currentStatus = MILD;
    }
  } else {
    currentStatus = NORMAL;
  }

  // Actions on status change
  if (currentStatus != lastStatus) {
    Serial.printf("[STATUS] Perubahan: %s -> %s\n", statusToString(lastStatus), statusToString(currentStatus));
    switch (currentStatus) {
      case NORMAL:
        stopBuzzer();
        setHeaterPower(0);
        mildIgnored = false;
        break;
      case MILD:
        if (!mildIgnored) {
          startBuzzer(1);
          BTSerial.println("{\"status\":\"WARNING\",\"warning_type\":\"MILD\",\"message\":\"Suhu tubuh rendah! Aktifkan pemanas?\"}");
        }
        break;
      case SEVERE:
        startBuzzer(2);
        setHeaterPower(100);
        mildIgnored = false;
        BTSerial.println("{\"status\":\"ALERT\",\"alert_type\":\"SEVERE\",\"message\":\"Kondisi kritis! Pemanas diaktifkan secara otomatis.\"}");
        break;
    }
    sendDataToApp(); // Send update immediately on status change
  } 
  // Actions for persistent status
  else { 
    if (currentStatus == MILD && mildIgnored) {
      const uint32_t MILD_REMINDER_INTERVAL_MS = 900000; // 15 minutes
      if (millis() - lastMildIgnoreTime >= MILD_REMINDER_INTERVAL_MS) {
        mildIgnored = false; // Re-notify
        checkHypothermiaStatus(); // Re-run logic to trigger notification
      }
    } else if (currentStatus == SEVERE) {
      checkRecovery();
    }
  }
}

void handleEmergencyButton() {
  bool buttonState = digitalRead(EMERGENCY_BTN);
  if (lastButtonState == HIGH && buttonState == LOW) {
    Serial.println("!!! TOMBOL DARURAT DITEKAN !!!");
    emergencyTriggered = true;
    sendLoraPacket(true);
    
    // Force SEVERE state
    currentStatus = SEVERE;
    checkHypothermiaStatus(); // Trigger immediate action for SEVERE
  }
  lastButtonState = buttonState;
}


// =================================
//      SETUP & LOOP
// =================================
void setup() {
  Serial.begin(115200);
  BTSerial.begin("JaketPemanas_TX");

  ds18b20.begin();
  mlx.begin();
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX, GPS_TX);

  pinMode(LORA_M0, OUTPUT);
  pinMode(LORA_M1, OUTPUT);
  digitalWrite(LORA_M0, LOW);
  digitalWrite(LORA_M1, LOW);
  loraSerial.begin(LORA_BAUD, SERIAL_8N1, LORA_RX, LORA_TX);

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(EMERGENCY_BTN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(HEATER_PIN, OUTPUT);

  digitalWrite(BUZZER_PIN, LOW);
  setHeaterPower(0);

  for (int i = 0; i < RECOVERY_WINDOW_SECONDS; i++) {
    tempHistory[i] = 37.0;
  }

  Serial.println("\n===================================");
  Serial.println("      NODE PENDAKI READY (TX2)");
  Serial.println("===================================");
}

void loop() {
  // --- Read all inputs ---
  while (gpsSerial.available()) gps.encode(gpsSerial.read());
  handleLoraIncoming();
  handleAppInput();
  handleSerialMonitorInput();
  handleEmergencyButton();

  // --- Read sensor data ---
  ds18b20.requestTemperatures();
  ambientTemp = ds18b20.getTempCByIndex(0);
  bodyTemp = 3 + mlx.readObjectTempC();
  
  // --- Process logic ---
  checkHypothermiaStatus();

  // --- Update history for recovery check ---
  if (millis() - lastHistoryUpdateTime >= 1000) {
    tempHistory[historyIndex] = bodyTemp;
    historyIndex = (historyIndex + 1) % RECOVERY_WINDOW_SECONDS;
    lastHistoryUpdateTime = millis();
  }

  // --- Send data periodically ---
  // Only send LoRa data if GPS is fixed
  if (gps.location.isValid() && gps.date.year() >= 2020) {
    if (millis() - lastLoraSend >= LORA_SEND_INTERVAL) {
      sendLoraPacket();
      lastLoraSend = millis();
    }
    if (millis() - lastLoraPing >= LORA_PING_INTERVAL) {
      sendLoraAckPing();
      lastLoraPing = millis();
    }
  }
  
  if (millis() - lastBtSendTime >= BT_SEND_INTERVAL_MS) {
    sendDataToApp();
    lastBtSendTime = millis();
  }

  // --- Update status indicators ---
  updateLedStatus();
  
  delay(10); // Small delay to prevent busy-waiting
}
