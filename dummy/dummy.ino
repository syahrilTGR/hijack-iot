#include <Arduino.h>
#include <BluetoothSerial.h>

#define LED_BUILTIN 2
#define LED_EMERGENCY 13

BluetoothSerial BTSerial;

uint32_t lastSend = 0;
uint32_t lastEmergency = 0;

void sendDummyJSON(float lat, float lon, float tempL, float tempT,
                   int heartRate, float spo2,
                   const char *timeStr, bool emergency) {
  BTSerial.printf(
    "{\"lat\":%.6f,\"lon\":%.6f,"
    "\"temp_lingkungan\":%.2f,"
    "\"temp_tubuh\":%.2f,"
    "\"heart_rate\":%d,"
    "\"spo2\":%.1f,"
    "\"time\":\"%s\","
    "\"emergency\":%s}\n",
    lat, lon, tempL, tempT, heartRate, spo2,
    timeStr, emergency ? "true" : "false"
  );
}

void setup() {
  Serial.begin(115200);
  BTSerial.begin("ESP32-RX-DUMMY");  // Nama Bluetooth untuk pairing di HP
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(LED_EMERGENCY, OUTPUT);
  Serial.println("=== RX DUMMY MODE ===");
  BTSerial.println("{\"status\":\"dummy_mode\"}");
}

void loop() {
  // Kirim data dummy setiap 3 detik
  if (millis() - lastSend > 3000) {
    lastSend = millis();

    // Data dummy lokasi & suhu
    float lat = -6.234567;
    float lon = 106.987654;
    float tempL = random(200, 300) / 10.0; // 20.0 - 30.0 °C
    float tempT = random(350, 380) / 10.0; // 35.0 - 38.0 °C

    // Data dummy MAX30102
    int heartRate = random(60, 101);       // 60 - 100 bpm
    float spo2 = random(950, 1000) / 10.0; // 95.0 - 100.0 %

    const char *timeStr = "2025-07-26 16:00:00";
    bool emergency = (random(0, 10) > 7); // 30% kemungkinan emergency

    // LED emergency simulasi
    if (emergency) {
      lastEmergency = millis();
      Serial.println("!!! EMERGENCY DUMMY !!!");
    }

    if (millis() - lastEmergency < 10000) {
      digitalWrite(LED_EMERGENCY, HIGH);
    } else {
      digitalWrite(LED_EMERGENCY, LOW);
    }

    // Blink LED built-in
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));

    // Debug Serial
    Serial.printf("[DUMMY] Lat: %.6f | Lon: %.6f | L: %.2f | T: %.2f | HR: %d | SpO2: %.1f | E: %d\n",
                  lat, lon, tempL, tempT, heartRate, spo2, emergency);

    // Kirim JSON ke Bluetooth
    sendDummyJSON(lat, lon, tempL, tempT, heartRate, spo2, timeStr, emergency);
  }
}
