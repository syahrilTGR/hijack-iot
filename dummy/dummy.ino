#include <Arduino.h>
#include <BluetoothSerial.h>
#include <WiFi.h>
#include <Preferences.h>

#define LED_BUILTIN 2
#define LED_EMERGENCY 13

BluetoothSerial BTSerial;
Preferences preferences;

uint32_t lastSend = 0;
uint32_t lastEmergency = 0;

String wifiSSID = "";
String wifiPASS = "";
bool wifiConfigured = false;
bool wifiConnected = false;

void sendDummyJSON(float lat, float lon, float tempL, float tempT,
                   const char *timeStr, bool emergency) {
  BTSerial.printf(
    "{\"lat\":%.6f,\"lon\":%.6f,"
    "\"temp_lingkungan\":%.2f,"
    "\"temp_tubuh\":%.2f,"
    "\"time\":\"%s\","
    "\"emergency\":%s}\n",
    lat, lon, tempL, tempT,
    timeStr, emergency ? "true" : "false"
  );
}

void connectToWiFi(const char* ssid, const char* password) {
  WiFi.begin(ssid, password);
  BTSerial.printf("{\"wifi\":\"connecting\",\"ssid\":\"%s\"}\n", ssid);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    BTSerial.printf("{\"wifi\":\"connected\",\"ip\":\"%s\"}\n", WiFi.localIP().toString().c_str());
    Serial.println("WiFi connected!");
    wifiConnected = true;
  } else {
    BTSerial.println("{\"wifi\":\"failed\"}");
    Serial.println("WiFi failed!");
    wifiConnected = false;
  }
}

void setup() {
  Serial.begin(115200);
  BTSerial.begin("ESP32-RX-DUMMY");  // Nama Bluetooth untuk pairing di HP
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(LED_EMERGENCY, OUTPUT);

  preferences.begin("wifi", false); // namespace "wifi"

  // Coba baca SSID & PASS dari Preferences
  wifiSSID = preferences.getString("ssid", "");
  wifiPASS = preferences.getString("pass", "");

  if (wifiSSID != "" && wifiPASS != "") {
    Serial.printf("Found saved WiFi: %s / %s\n", wifiSSID.c_str(), wifiPASS.c_str());
    connectToWiFi(wifiSSID.c_str(), wifiPASS.c_str());
  } else {
    Serial.println("No WiFi config saved. Use Bluetooth to set.");
  }

  Serial.println("=== RX DUMMY MODE with WiFi config + Preferences ===");
  BTSerial.println("{\"status\":\"dummy_mode_bt_ready\"}");
  BTSerial.println("{\"info\":\"Ketik: WIFI:SSID,PASS untuk konek WiFi\"}");
}

void loop() {
  // Cek apakah ada data dari Bluetooth (untuk WiFi config)
  if (BTSerial.available()) {
    String cmd = BTSerial.readStringUntil('\n');
    cmd.trim();

    if (cmd.startsWith("WIFI:")) {
      int commaIndex = cmd.indexOf(',');
      if (commaIndex > 5) {
        wifiSSID = cmd.substring(5, commaIndex);
        wifiPASS = cmd.substring(commaIndex + 1);
        wifiConfigured = true;

        BTSerial.printf("{\"wifi_ssid\":\"%s\",\"status\":\"received\"}\n", wifiSSID.c_str());
        Serial.printf("SSID: %s | PASS: %s\n", wifiSSID.c_str(), wifiPASS.c_str());

        connectToWiFi(wifiSSID.c_str(), wifiPASS.c_str());

        if (wifiConnected) {
          // Simpan ke Preferences
          preferences.putString("ssid", wifiSSID);
          preferences.putString("pass", wifiPASS);
          BTSerial.println("{\"wifi\":\"saved\"}");
          Serial.println("WiFi credentials saved!");
        }
      } else {
        BTSerial.println("{\"error\":\"format salah. Gunakan WIFI:SSID,PASS\"}");
      }
    }
  }

  // Kirim data dummy setiap 3 detik
  if (millis() - lastSend > 3000) {
    lastSend = millis();

    float lat = -6.234567;
    float lon = 106.987654;
    float tempL = random(200, 300) / 10.0;
    float tempT = random(350, 380) / 10.0;

    const char *timeStr = "2025-07-26 16:00:00";
    bool emergency = (random(0, 10) > 7);

    if (emergency) {
      lastEmergency = millis();
      Serial.println("!!! EMERGENCY DUMMY !!!");
    }

    if (millis() - lastEmergency < 10000) {
      digitalWrite(LED_EMERGENCY, HIGH);
    } else {
      digitalWrite(LED_EMERGENCY, LOW);
    }

    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));

    Serial.printf("[DUMMY] Lat: %.6f | Lon: %.6f | L: %.2f | T: %.2f | E: %d\n",
                  lat, lon, tempL, tempT, emergency);

    sendDummyJSON(lat, lon, tempL, tempT, timeStr, emergency);
  }
}
