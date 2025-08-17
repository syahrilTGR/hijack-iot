#include <Arduino.h>
#include <BluetoothSerial.h>
#include <WiFi.h>
#include <Preferences.h>
#include <WebServer.h>

#define LED_BUILTIN 2
#define LED_EMERGENCY 13
#define BUTTON_EMERGENCY 34  // Tombol emergency

BluetoothSerial BTSerial;
Preferences prefs;
WebServer server(80);

uint32_t lastSend = 0;
uint32_t lastEmergency = 0;

// Koordinat pusat
const double baseLat = -7.946026224590281;
const double baseLon = 112.6157531157512;

String wifiSSID = "";
String wifiPASS = "";

// Data terakhir untuk API
float lastLat, lastLon, lastTempL, lastTempT;
bool lastEmergencyFlag;
String lastTime = "2025-08-17 15:00:00";

void sendDummyJSON_BT(float lat, float lon, float tempL, float tempT,
                      const char *timeStr, bool emergency) {
  BTSerial.printf(
    "{\"lat\":%.6f,\"lon\":%.6f,"
    "\"temp_lingkungan\":%.2f,"
    "\"temp_tubuh\":%.2f,"
    "\"time\":\"%s\","
    "\"emergency\":%s}\n",
    lat, lon, tempL, tempT, timeStr, emergency ? "true" : "false"
  );
}

void sendDummyJSON_HTTP() {
  String json = "{";
  json += "\"lat\":" + String(lastLat, 6) + ",";
  json += "\"lon\":" + String(lastLon, 6) + ",";
  json += "\"temp_lingkungan\":" + String(lastTempL, 2) + ",";
  json += "\"temp_tubuh\":" + String(lastTempT, 2) + ",";
  json += "\"time\":\"" + lastTime + "\",";
  json += "\"emergency\":" + String(lastEmergencyFlag ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

void connectWiFi() {
  if (wifiSSID.length() == 0 || wifiPASS.length() == 0) return;
  WiFi.begin(wifiSSID.c_str(), wifiPASS.c_str());
  BTSerial.printf("{\"wifi\":\"connecting\",\"ssid\":\"%s\"}\n", wifiSSID.c_str());
  Serial.printf("Connecting to WiFi SSID: %s\n", wifiSSID.c_str());
  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nWiFi Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    BTSerial.printf("{\"wifi\":\"connected\",\"ip\":\"%s\"}\n", WiFi.localIP().toString().c_str());

    // Setup web server endpoint
    server.on("/data", sendDummyJSON_HTTP);
    server.begin();
    Serial.println("HTTP server started");
  } else {
    Serial.println("\nWiFi Failed!");
    BTSerial.println("{\"wifi\":\"failed\"}");
  }
}

void saveWiFiCredentials(const String &ssid, const String &pass) {
  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
  Serial.println("WiFi credentials saved!");
}

void loadWiFiCredentials() {
  prefs.begin("wifi", true);
  wifiSSID = prefs.getString("ssid", "");
  wifiPASS = prefs.getString("pass", "");
  prefs.end();
  if (wifiSSID.length() > 0) {
    Serial.printf("Loaded SSID: %s\n", wifiSSID.c_str());
  } else {
    Serial.println("No WiFi credentials stored.");
  }
}

void setup() {
  Serial.begin(115200);
  BTSerial.begin("ESP32-RX-DUMMY");
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(LED_EMERGENCY, OUTPUT);
  pinMode(BUTTON_EMERGENCY, INPUT_PULLUP);

  Serial.println("=== RX DUMMY MODE with WiFi + HTTP API (No MAX30102) ===");
  BTSerial.println("{\"status\":\"dummy_mode_wifi_http_no_max\"}");

  loadWiFiCredentials();
  if (wifiSSID.length() > 0 && wifiPASS.length() > 0) {
    connectWiFi();
  }
}

void loop() {
  // === Handle input dari Bluetooth ===
  if (BTSerial.available()) {
    String input = BTSerial.readStringUntil('\n');
    input.trim();

    if (input.startsWith("WIFI:")) {
      int commaIndex = input.indexOf(',');
      if (commaIndex > 5) {
        wifiSSID = input.substring(5, commaIndex);
        wifiPASS = input.substring(commaIndex + 1);
        saveWiFiCredentials(wifiSSID, wifiPASS);
        connectWiFi();
      }
    } else if (input == "WIFI:RESET") {
      prefs.begin("wifi", false);
      prefs.clear();
      prefs.end();
      wifiSSID = "";
      wifiPASS = "";
      Serial.println("WiFi credentials erased!");
      BTSerial.println("{\"wifi\":\"cleared\"}");
    } else if (input == "GET_IP") {
      if (WiFi.status() == WL_CONNECTED) {
        BTSerial.printf("{\"ip\":\"%s\"}\n", WiFi.localIP().toString().c_str());
      } else {
        BTSerial.println("{\"ip\":null}");
      }
    }
  }

  // === Kirim data dummy setiap 3 detik ===
  if (millis() - lastSend > 3000) {
    lastSend = millis();

    // Koordinat dummy ±50m
    float latOffset = ((random(-1000, 1001) / 1000.0) * 0.00045);
    float lonOffset = ((random(-1000, 1001) / 1000.0) * 0.00045);
    lastLat = baseLat + latOffset;
    lastLon = baseLon + lonOffset;

    // Data dummy
    lastTempL = random(200, 300) / 10.0;
    lastTempT = random(350, 380) / 10.0;

    // Emergency
    bool emergencyButton = (digitalRead(BUTTON_EMERGENCY) == LOW);
    bool emergencyRand = (random(0, 10) > 7);
    lastEmergencyFlag = emergencyButton || emergencyRand;

    if (lastEmergencyFlag) {
      lastEmergency = millis();
      Serial.println("!!! EMERGENCY TRIGGERED !!!");
    }
    if (millis() - lastEmergency < 10000) {
      digitalWrite(LED_EMERGENCY, HIGH);
    } else {
      digitalWrite(LED_EMERGENCY, LOW);
    }

    // Blink LED built-in
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));

    // Debug
    Serial.printf("[DUMMY] Lat: %.6f | Lon: %.6f | L: %.2f | T: %.2f | E: %d\n",
                  lastLat, lastLon, lastTempL, lastTempT, lastEmergencyFlag);

    // Kirim JSON ke Bluetooth
    sendDummyJSON_BT(lastLat, lastLon, lastTempL, lastTempT, lastTime.c_str(), lastEmergencyFlag);
  }

  // Handle HTTP request
  if (WiFi.status() == WL_CONNECTED) {
    server.handleClient();
  }
}
