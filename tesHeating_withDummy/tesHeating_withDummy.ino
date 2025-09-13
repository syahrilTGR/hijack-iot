#include <BluetoothSerial.h>

// ===== LORA ADDRESSING =====
#define MY_ADDRESS 1
#define REPEATER_ADDRESS 2

// ===== PIN DEFINITIONS =====
#define EMERGENCY_BTN 15
#define BUZZER_PIN 25
#define HEATER_PIN 23
#define LED_BUILTIN 2

// ===== LORA DEFINITIONS =====
#define LORA_RX 26
#define LORA_TX 27
#define LORA_BAUD 9600
#define LORA_M0 14
#define LORA_M1 12

// ===== OBJECTS =====
HardwareSerial loraSerial(1);
BluetoothSerial BTSerial;

// ===== TIMING & STATE =====
const uint32_t SEND_INTERVAL = 5000;
const uint32_t PING_INTERVAL = 2000;
const uint32_t EMERGENCY_TIMEOUT = 30000; // 30 detik
uint32_t lastSend = 0;
uint32_t lastPing = 0;
uint32_t lastAckTime = 0;
bool lastButtonState = HIGH;

// --- Buzzer state ---
bool buzzerActive = false;
uint32_t buzzerStartTime = 0;
const uint32_t BUZZER_DURATION = 1000;

// --- Emergency state ---
bool emergencyActive = false;
uint32_t emergencyStart = 0;

// --- Heater state ---
int heaterPowerPercentage = 0;

// =================================
// BLUETOOTH & HEATER FUNCTIONS
// =================================

void setHeaterPower(int percentage, const char* reason = "command") {
  heaterPowerPercentage = constrain(percentage, 0, 100);
  int dutyCycle = map(heaterPowerPercentage, 0, 100, 0, 255);
  analogWrite(HEATER_PIN, dutyCycle);
  
  Serial.printf("[HEATER] Power diatur ke %d%% (Alasan: %s)\n", heaterPowerPercentage, reason);

  if (strcmp(reason, "command") != 0) {
    BTSerial.printf("{\"event\":\"power_override\",\"power\":%d,\"reason\":\"%s\"}\n", heaterPowerPercentage, reason);
  } else {
    BTSerial.printf("{\"event\":\"power_update\",\"status\":\"success\",\"power\":%d}\n", heaterPowerPercentage);
  }
}

void handleBluetooth() {
  if (BTSerial.available()) {
    String cmd = BTSerial.readStringUntil('\n');
    cmd.trim();
    Serial.printf("[BT] Perintah diterima: %s\n", cmd.c_str());

    if (cmd.startsWith("PWM:")) {
      int power = cmd.substring(4).toInt();
      setHeaterPower(power, "command");
    } else if (cmd == "GET_STATUS") {
      BTSerial.printf("{\"event\":\"status_report\",\"power\":%d,\"emergency\":%s}\n", heaterPowerPercentage, emergencyActive ? "true" : "false");
    }
    else {
      BTSerial.println("{\"event\":\"error\",\"message\":\"unknown_command\"}");
    }
  }
}

// =================================
// LORA & DATA FUNCTIONS
// =================================

String buildLocalTime() {
    return "2025-01-01 12:00:00";
}

void sendPacket(float tempLingkungan, float tempTubuh, bool emergency = false) {
  String payload;
  String header = String(REPEATER_ADDRESS) + "," + String(MY_ADDRESS) + ",";

  payload = "0.000000,0.000000," +
            String(tempLingkungan, 2) + "," +
            String(tempTubuh, 2) + "," +
            buildLocalTime();

  if (emergency) payload += ",EMERGENCY";

  String fullMessage = header + payload + "\n";
  loraSerial.print(fullMessage);
  Serial.print("Kirim Data: ");
  Serial.print(fullMessage);
}

void sendAckPing() {
  String pingMessage = String(REPEATER_ADDRESS) + "," + String(MY_ADDRESS) + ",ACK\n";
  loraSerial.print(pingMessage);
  Serial.println("[TX] ACK/Ping dikirim ke Repeater");
}

void handleLoRaIncoming() {
  static String buffer;
  while (loraSerial.available()) {
    char c = loraSerial.read();
    if (c == '\n') {
      int destAddr, srcAddr;
      char payload[20];
      sscanf(buffer.c_str(), "%d,%d,%19s", &destAddr, &srcAddr, payload);

      if (destAddr == MY_ADDRESS && String(payload) == "ACK") {
        lastAckTime = millis();
        Serial.printf("[TX] ACK diterima dari Addr %d\n", srcAddr);
      }
      buffer = "";
    } else if (c != '\r') {
      buffer += c;
      if (buffer.length() > 50) buffer = "";
    }
  }
}

// =================================
// UTILITY FUNCTIONS
// =================================

void updateLED() {
  static uint32_t lastToggle = 0;
  static bool ledState = false;
  if (millis() - lastAckTime < 4000) {
    if (millis() - lastToggle >= 1000) {
      ledState = !ledState;
      digitalWrite(LED_BUILTIN, ledState);
      lastToggle = millis();
    }
  } else {
    digitalWrite(LED_BUILTIN, LOW);
  }
}

void updateBuzzer() {
  if (buzzerActive && millis() - buzzerStartTime >= BUZZER_DURATION) {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerActive = false;
  }
}

// =================================
// SETUP & LOOP
// =================================

void setup() {
  Serial.begin(115200);
  
  // Mulai Bluetooth
  BTSerial.begin("ESP32-Heater-Dummy");
  Serial.println("Perangkat Bluetooth siap.");

  // Konfigurasi LoRa
  pinMode(LORA_M0, OUTPUT);
  pinMode(LORA_M1, OUTPUT);
  digitalWrite(LORA_M0, LOW);
  digitalWrite(LORA_M1, LOW);
  loraSerial.begin(LORA_BAUD, SERIAL_8N1, LORA_RX, LORA_TX);

  // Konfigurasi Pin-pin
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(EMERGENCY_BTN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(HEATER_PIN, OUTPUT); // Atur pin heater sebagai output

  // Atur daya heater ke 0 saat awal
  setHeaterPower(0, "init");

  Serial.println("=== NODE TX - HEATER DUMMY MODE ===");
  BTSerial.println("{\"event\":\"ready\"}");
}

void loop() {
  // 1. Handle input dari Bluetooth
  handleBluetooth();

  // 2. Generate data sensor dummy
  float tempLingkungan = random(200, 280) / 10.0; // 20.0 - 28.0 C
  float tempTubuh = random(360, 375) / 10.0;    // 36.0 - 37.5 C

  // 3. Cek tombol darurat
  bool buttonState = digitalRead(EMERGENCY_BTN);
  if (lastButtonState == HIGH && buttonState == LOW) {
    Serial.println("!!! TOMBOL DARURAT DITEKAN !!!");
    if (!emergencyActive) {
        emergencyActive = true;
        emergencyStart = millis();
        setHeaterPower(100, "emergency_start");
        digitalWrite(BUZZER_PIN, HIGH);
        buzzerActive = true;
        buzzerStartTime = millis();
    }
  }
  lastButtonState = buttonState;

  // 4. Reset status darurat setelah timeout
  if (emergencyActive && millis() - emergencyStart > EMERGENCY_TIMEOUT) {
    emergencyActive = false;
    setHeaterPower(0, "emergency_end");
    Serial.println("[TX] Emergency otomatis reset.");
  }

  // 5. Kirim paket data LoRa secara periodik
  if (millis() - lastSend >= SEND_INTERVAL) {
    sendPacket(tempLingkungan, tempTubuh, emergencyActive);
    lastSend = millis();
  }

  // 6. Kirim ping LoRa
  if (millis() - lastPing >= PING_INTERVAL) {
    sendAckPing();
    lastPing = millis();
  }

  // 7. Handle LoRa incoming & update status
  handleLoRaIncoming();
  updateLED();
  updateBuzzer();
}
