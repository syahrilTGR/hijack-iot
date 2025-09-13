#include <OneWire.h>
#include <DallasTemperature.h>
#include <TinyGPS++.h>
#include <Adafruit_MLX90614.h>

// ===== LORA ADDRESSING =====
#define MY_ADDRESS 1
#define REPEATER_ADDRESS 2

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

OneWire oneWire(DS18B20_PIN);
DallasTemperature ds18b20(&oneWire);
Adafruit_MLX90614 mlx = Adafruit_MLX90614();
TinyGPSPlus gps;

HardwareSerial loraSerial(1);
HardwareSerial gpsSerial(2);

const uint32_t SEND_INTERVAL = 5000;
const uint32_t PING_INTERVAL = 2000;

uint32_t lastSend = 0;
uint32_t lastPing = 0;
uint32_t lastAckTime = 0;

bool lastButtonState = HIGH;

// --- variabel buzzer ---
bool buzzerActive = false;
uint32_t buzzerStartTime = 0;
const uint32_t BUZZER_DURATION = 1000; // 1 detik

// --- variabel heating pad ---
bool heaterActive = false;            // untuk mode darurat
uint32_t heaterStartTime = 0;
const uint32_t HEATER_DURATION = 5000; // 5 detik nyala (darurat)

// --- variabel heater otomatis ---
bool heaterAuto = false;

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

void sendPacket(float tempLingkungan, float tempTubuh, bool emergency = false) {
  String payload;
  String header = String(REPEATER_ADDRESS) + "," + String(MY_ADDRESS) + ",";

  if (gps.location.isValid() && gps.date.year() >= 2020) {
    payload = String(gps.location.lat(), 6) + "," +
              String(gps.location.lng(), 6) + "," +
              String(tempLingkungan, 2) + "," +
              String(tempTubuh, 2) + "," +
              buildLocalTime();
  } else {
    payload = "0,0," +
              String(tempLingkungan, 2) + "," +
              String(tempTubuh, 2) + ",0000-00-00 00:00:00";
  }

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

void handleIncoming() {
  static String buffer;
  while (loraSerial.available()) {
    char c = loraSerial.read();
    if (c == '\n') {
      // Format pesan: DEST,SRC,PAYLOAD
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

void updateHeater() {
  // Mode darurat (nyala 5 detik)
  if (heaterActive && millis() - heaterStartTime >= HEATER_DURATION) {
    digitalWrite(HEATER_PIN, LOW);
    heaterActive = false;
    Serial.println("[HEATER] Dimatikan otomatis (darurat selesai)");
  }

  // Mode otomatis: ON terus bila suhu tubuh < 35
  if (heaterAuto) {
    digitalWrite(HEATER_PIN, HIGH);
  }
}

void setup() {
  Serial.begin(115200);
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
  digitalWrite(BUZZER_PIN, LOW);

  pinMode(HEATER_PIN, OUTPUT);
  digitalWrite(HEATER_PIN, LOW);

  Serial.println("=== NODE PENDAKI READY (TX) ===");
}

void loop() {
  while (gpsSerial.available()) gps.encode(gpsSerial.read());
  ds18b20.requestTemperatures();
  float tempLingkungan = ds18b20.getTempCByIndex(0);
  float tempTubuh = mlx.readObjectTempC();

  static uint32_t lastDebug = 0;
  if (millis() - lastDebug >= 2000) {
    Serial.printf("Suhu Lingkungan: %.2f °C | Suhu Tubuh: %.2f °C | GPS Valid: %d\n", 
                  tempLingkungan, tempTubuh, gps.location.isValid());
    lastDebug = millis();
  }

  // === Heater otomatis berdasarkan suhu tubuh ===
  if (tempTubuh < 35.0) {
    if (!heaterAuto) {
      Serial.println("[HEATER] Otomatis ON (suhu tubuh < 35)");
      heaterAuto = true;
    }
  } else {
    if (heaterAuto) {
      Serial.println("[HEATER] Otomatis OFF (suhu tubuh >= 35)");
      heaterAuto = false;
      // pastikan mati jika tidak ada darurat aktif
      if (!heaterActive) digitalWrite(HEATER_PIN, LOW);
    }
  }

  // === Tombol darurat ===
  bool buttonState = digitalRead(EMERGENCY_BTN);
  if (lastButtonState == HIGH && buttonState == LOW) {
    Serial.println("!!! TOMBOL DARURAT DITEKAN !!!");
    sendPacket(tempLingkungan, tempTubuh, true);

    // Aktifkan buzzer
    digitalWrite(BUZZER_PIN, HIGH);
    buzzerActive = true;
    buzzerStartTime = millis();

    // Aktifkan heater 5 detik (mode darurat)
    digitalWrite(HEATER_PIN, HIGH);
    heaterActive = true;
    heaterStartTime = millis();
    Serial.println("[HEATER] Dinyalakan 5 detik (darurat)");
  }
  lastButtonState = buttonState;

  if (millis() - lastSend >= SEND_INTERVAL) {
    sendPacket(tempLingkungan, tempTubuh);
    lastSend = millis();
  }

  if (millis() - lastPing >= PING_INTERVAL) {
    sendAckPing();
    lastPing = millis();
  }

  handleIncoming();
  updateLED();
  updateBuzzer();
  updateHeater();
}
