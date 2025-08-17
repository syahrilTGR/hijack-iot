#include <OneWire.h>
#include <DallasTemperature.h>
#include <TinyGPS++.h>
#include <Adafruit_MLX90614.h>

#define DS18B20_PIN 4
#define EMERGENCY_BTN 15
#define BUZZER_PIN 25

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

  payload += "\n";
  loraSerial.print(payload);
  Serial.print("Kirim Data: ");
  Serial.print(payload);
}

void sendAckPing() {
  loraSerial.print("ACK\n");
  Serial.println("[TX] ACK dikirim ke RX");
}

void handleIncoming() {
  static String buffer;
  while (loraSerial.available()) {
    char c = loraSerial.read();
    if (c == '\n') {
      if (buffer == "ACK") {
        lastAckTime = millis();
        Serial.println("[TX] ACK diterima dari RX");
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

  bool buttonState = digitalRead(EMERGENCY_BTN);
  if (lastButtonState == HIGH && buttonState == LOW) {
    Serial.println("!!! TOMBOL DARURAT DITEKAN !!!");
    sendPacket(tempLingkungan, tempTubuh, true);

    // Aktifkan buzzer non-blocking
    digitalWrite(BUZZER_PIN, HIGH);
    buzzerActive = true;
    buzzerStartTime = millis();
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
}