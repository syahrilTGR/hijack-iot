#include <Arduino.h>
#include <BluetoothSerial.h>

#define LORA_RX 16
#define LORA_TX 17
#define LORA_BAUD 9600
#define LORA_M0 4
#define LORA_M1 5
#define LED_BUILTIN 2
#define LED_EMERGENCY 13

HardwareSerial loraSerial(2);
BluetoothSerial BTSerial;

uint32_t lastAckSend = 0;
uint32_t lastAckRecv = 0;
uint32_t lastEmergency = 0;

void sendAck() {
  loraSerial.print("ACK\n");
  Serial.println("[RX] ACK dibalas ke TX");
  //BTSerial.println("{\"ack\":true}");
  lastAckSend = millis();
}

void updateLED() {
  static uint32_t lastToggle = 0;
  static bool ledState = false;

  if (millis() - lastAckRecv < 4000) {
    if (millis() - lastToggle >= 1000) {
      ledState = !ledState;
      digitalWrite(LED_BUILTIN, ledState);
      lastToggle = millis();
    }
  } else {
    digitalWrite(LED_BUILTIN, LOW);
  }

  if (millis() - lastEmergency < 10000) {
    digitalWrite(LED_EMERGENCY, HIGH);
  } else {
    digitalWrite(LED_EMERGENCY, LOW);
  }
}

// **//void sendJSON(float lat, float lon, float tempL, float tempT, const char *timeStr, bool emergency) {
//   // Kirim JSON ke Bluetooth
//   BTSerial.printf("{");
//   BTSerial.printf("\"lat\":%.6f,", lat);
//   BTSerial.printf("\"lon\":%.6f,", lon);
//   BTSerial.printf("\"temp_lingkungan\":%.2f,", tempL);
//   BTSerial.printf("\"temp_tubuh\":%.2f,", tempT);
//   BTSerial.printf("\"time\":\"%s\",", timeStr);
//   BTSerial.printf("\"emergency\":%s", emergency ? "true" : "false");
//   BTSerial.println("}");
// }//**
void sendJSON(float lat, float lon, float tempL, float tempT, const char *timeStr, bool emergency) {
  // Satu printf JSON lengkap
  BTSerial.printf(
    "{\"lat\":%.6f,\"lon\":%.6f,\"temp_lingkungan\":%.2f,"
    "\"temp_tubuh\":%.2f,\"time\":\"%s\",\"emergency\":%s}\n",
    lat, lon, tempL, tempT, timeStr, emergency ? "true" : "false"
  );
}

void handlePacket(const String &packet) {
  Serial.printf("Paket: %s\n", packet.c_str());

  float lat, lon, tempLingkungan, tempTubuh;
  char timeStr[30];
  char status[15] = "";
  bool emergencyFlag = false;

  int parsed = sscanf(packet.c_str(), "%f,%f,%f,%f,%29[^,\n],%14s",
                      &lat, &lon, &tempLingkungan, &tempTubuh, timeStr, status);

  if (parsed >= 5) {
    if (parsed == 6 && String(status) == "EMERGENCY") {
      emergencyFlag = true;
      Serial.println("!!! SINYAL DARURAT DITERIMA !!!");
      lastEmergency = millis();
    }

    // Debug ke Serial Monitor
    Serial.printf("Latitude: %.6f | Longitude: %.6f\n", lat, lon);
    Serial.printf("Suhu Lingkungan: %.2f °C | Suhu Tubuh: %.2f °C\n", tempLingkungan, tempTubuh);
    Serial.printf("Waktu: %s | Emergency: %d\n", timeStr, emergencyFlag);
    Serial.println("----------------------------");

    // Kirim data JSON ke Bluetooth
    sendJSON(lat, lon, tempLingkungan, tempTubuh, timeStr, emergencyFlag);

  } else {
    Serial.println("! Format tidak valid");
    //BTSerial.println("{\"error\":\"invalid_format\"}");
  }
}

void setup() {
  Serial.begin(115200);
  BTSerial.begin("ESP32-RX");  // Nama Bluetooth untuk pairing di HP

  pinMode(LORA_M0, OUTPUT);
  pinMode(LORA_M1, OUTPUT);
  digitalWrite(LORA_M0, LOW);
  digitalWrite(LORA_M1, LOW);

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(LED_EMERGENCY, OUTPUT);

  loraSerial.begin(LORA_BAUD, SERIAL_8N1, LORA_RX, LORA_TX);

  Serial.println("=== POS PENJAGA READY (RX) ===");
  //BTSerial.println("{\"status\":\"ready\"}");
}

void loop() {
  static String buf;

  while (loraSerial.available()) {
    char c = loraSerial.read();
    if (c == '\n') {
      if (buf.length()) {
        if (buf == "ACK") {
          Serial.println("[RX] Terima ACK/ping dari TX");
          //BTSerial.println("{\"ack_received\":true}");
          lastAckRecv = millis();
          sendAck();
        } else {
          handlePacket(buf);
        }
        buf = "";
      }
    } else if (c != '\r') {
      buf += c;
      if (buf.length() > 150) buf = "";
    }
  }

  updateLED();
}
