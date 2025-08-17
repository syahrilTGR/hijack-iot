#include <Arduino.h>

#define LORA_RX 16
#define LORA_TX 17
#define LORA_BAUD 9600
#define LORA_M0 4
#define LORA_M1 5
#define LED_BUILTIN 2
#define LED_EMERGENCY 13

HardwareSerial loraSerial(2);

uint32_t lastAckSend = 0;
uint32_t lastAckRecv = 0;
uint32_t lastEmergency = 0;

void sendAck() {
  loraSerial.print("ACK\n");
  Serial.println("[RX] ACK dibalas ke TX");
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

void handlePacket(const String &packet) {
  Serial.printf("Paket: %s\n", packet.c_str());

  float lat, lon, tempLingkungan, tempTubuh;
  char timeStr[30];
  char status[15] = "";

  int parsed = sscanf(packet.c_str(), "%f,%f,%f,%f,%29[^,\n],%14s",
                      &lat, &lon, &tempLingkungan, &tempTubuh, timeStr, status);

  if (parsed >= 5) {
    if (!isnan(lat) && !isnan(lon))
      Serial.printf("Latitude       : %.6f°\nLongitude      : %.6f°\n", lat, lon);

    Serial.printf("Suhu Lingkungan: %.2f °C\n", tempLingkungan);
    Serial.printf("Suhu Tubuh     : %.2f °C\n", tempTubuh);
    Serial.printf("Waktu          : %s\n", timeStr);

    if (parsed == 6 && strcmp(status, "EMERGENCY") == 0) {
      Serial.println("!!! SINYAL DARURAT DITERIMA !!!");
      lastEmergency = millis();
    }

    Serial.println("----------------------------");
  } else {
    Serial.println("! Format tidak valid");
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(LORA_M0, OUTPUT);
  pinMode(LORA_M1, OUTPUT);
  digitalWrite(LORA_M0, LOW);
  digitalWrite(LORA_M1, LOW);

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(LED_EMERGENCY, OUTPUT);

  loraSerial.begin(LORA_BAUD, SERIAL_8N1, LORA_RX, LORA_TX);
  Serial.println("=== POS PENJAGA READY (RX) ===");
}

void loop() {
  static String buf;

  while (loraSerial.available()) {
    char c = loraSerial.read();
    if (c == '\n') {
      if (buf.length()) {
        if (buf == "ACK") {
          Serial.println("[RX] Terima ACK/ping dari TX");
          lastAckRecv = millis();
          sendAck();
        } else {
          handlePacket(buf);
        }
        buf = "";
      }
    } else if (c != '\r') {
      buf += c;
      if (buf.length() > 200) buf = "";
    }
  }

  updateLED();
}
