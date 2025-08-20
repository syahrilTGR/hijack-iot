#include <Arduino.h>
#include <BluetoothSerial.h>
#include <WiFi.h>
#include <WebSocketsServer.h>
#include <Preferences.h>
#include <math.h>

BluetoothSerial BTSerial;
Preferences preferences;

// ===== Pin Definitions =====
const int BUZZER_PIN = 2; // Ubah ke pin buzzer yang benar
const int LED_BUILTIN = 2; // Menggunakan LED bawaan untuk simulasi

// ===== WebSocket Server =====
WebSocketsServer webSocket = WebSocketsServer(81);
bool wsStarted = false;

// ===== WiFi Credentials =====
String wifiSSID = "";
String wifiPASS = "";
bool wifiConnected = false;

// ===== User Preferences =====
int userAge = 0;
String userGender = "unknown";
bool userHijab = false;

// ===== Buzzer Control =====
void updateBuzzerStatus(float temp) {
  if (temp < 35.0) {
    digitalWrite(LED_BUILTIN, HIGH);
  } else {
    digitalWrite(LED_BUILTIN, LOW);
  }
}

// ===== Dummy JSON dengan koordinat ±100m =====
void createDummyJSON(char* buf, size_t len) {
  const double baseLat = -7.94621154560041;
  const double baseLon = 112.61544899535379;

  // Geser ±100 meter random
  double deltaLat_m = ((double)random(-100, 101));
  double deltaLon_m = ((double)random(-100, 101));

  double lat = baseLat + (deltaLat_m / 111139.0);
  double lon = baseLon + (deltaLon_m / (111320.0 * cos(baseLat * DEG_TO_RAD)));

  float tempL = random(200, 300) / 10.0;
  float tempT = random(330, 380) / 10.0;
  float originalTempT = tempT; // Simpan nilai asli

  // Apply temperature offset if user wears hijab
  if (userHijab) {
    tempT += 2.0; // Tambah 2 derajat Celsius
    Serial.printf("Suhu Asli (dengan hijab): %.1f C\n", originalTempT);
  }

  bool emergency = (tempT < 35.0);

  // Perbarui status buzzer di sini, sebelum JSON dibuat
  updateBuzzerStatus(tempT);

  snprintf(buf, len,
           "{\"lat\":%.6f,\"lon\":%.6f,\"temp_lingkungan\":%.1f,\"temp_tubuh\":%.1f,\"time\":\"2025-07-26 16:00:00\",\"emergency\":%s,\"age\":%d,\"gender\":\"%s\",\"hijab\":%s}",
           lat, lon, tempL, tempT, emergency ? "true" : "false", userAge, userGender.c_str(), userHijab ? "true" : "false");
}

// ===== WebSocket Events =====
void onWebSocketEvent(uint8_t clientNum, WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      Serial.printf("WebSocket client %u connected\n", clientNum);
      char buf[256];
      createDummyJSON(buf, sizeof(buf));
      webSocket.sendTXT(clientNum, buf);
      break;
    case WStype_DISCONNECTED:
      Serial.printf("WebSocket client %u disconnected\n", clientNum);
      break;
    case WStype_TEXT:
      Serial.printf("WebSocket client %u sent text: %s\n", clientNum, payload);
      break;
  }
}

// ===== Connect to WiFi =====
void connectToWiFi(const char* ssid, const char* pass) {
  WiFi.disconnect(true);
  delay(500);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  Serial.printf("Connecting to WiFi: %s\n", ssid);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    BTSerial.printf("{\"wifi\":\"connected\",\"ip\":\"%s\"}\n", WiFi.localIP().toString().c_str());
  } else {
    wifiConnected = false;
    Serial.println("\nWiFi connection failed!");
    BTSerial.println("{\"wifi\":\"failed\"}");
  }
}

// ===== WebSocket Task =====
void webSocketTask(void *pvParameters) {
  char buf[256];
  for (;;) {
    if (wsStarted) {
      webSocket.loop();
    }
    if (wifiConnected && wsStarted) {
      static unsigned long lastSend = 0;
      if (millis() - lastSend > 2000) {
        lastSend = millis();
        createDummyJSON(buf, sizeof(buf));
        webSocket.broadcastTXT(buf);
        Serial.printf("Broadcast: %s\n", buf);
      }
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

// ===== Setup =====
void setup() {
  Serial.begin(115200);
  delay(100);
  BTSerial.begin("ESP32-RX-DUMMY");
  pinMode(LED_BUILTIN, OUTPUT);
  delay(200);

  Serial.println("=== Bluetooth ready for input ===");
  BTSerial.println("{\"info\":\"Ketik WIFI:SSID,PASS | USER:AGE,GENDER,HIJAB | GETIP | RESET\"}");

  // Load preferences
  preferences.begin("data", false);
  wifiSSID = preferences.getString("ssid", "");
  wifiPASS = preferences.getString("pass", "");
  userAge = preferences.getInt("age", 0);
  userGender = preferences.getString("gender", "unknown");
  userHijab = preferences.getBool("hijab", false);
  preferences.end();

  if (wifiSSID.length() > 0 && wifiPASS.length() > 0) {
    Serial.printf("Mendapat SSID dari Preferences: %s\n", wifiSSID.c_str());
    connectToWiFi(wifiSSID.c_str(), wifiPASS.c_str());
    if (wifiConnected && !wsStarted) {
      webSocket.begin();
      webSocket.onEvent(onWebSocketEvent);
      wsStarted = true;
      Serial.println("WebSocket server started on port 81");
    }
  }

  // Start WebSocket task
  xTaskCreate(webSocketTask, "wsTask", 8192, NULL, 1, NULL);
}

unsigned long lastWifiStatusSend = 0;

// ===== Loop =====
void loop() {
  // Bluetooth input
  if (BTSerial.available()) {
    String cmd = BTSerial.readStringUntil('\n');
    cmd.trim();

    if (cmd.startsWith("WIFI:")) {
      int commaIndex = cmd.indexOf(',');
      if (commaIndex > 5) {
        wifiSSID = cmd.substring(5, commaIndex);
        wifiPASS = cmd.substring(commaIndex + 1);
        preferences.begin("data", false);
        preferences.putString("ssid", wifiSSID);
        preferences.putString("pass", wifiPASS);
        preferences.end();
        connectToWiFi(wifiSSID.c_str(), wifiPASS.c_str());
        if (wifiConnected && !wsStarted) {
          webSocket.begin();
          webSocket.onEvent(onWebSocketEvent);
          wsStarted = true;
          Serial.println("WebSocket server started on port 81");
        }
      } else {
        BTSerial.println("{\"error\":\"format salah. Gunakan WIFI:SSID,PASS\"}");
      }
    } else if (cmd.startsWith("USER:")) {
      int firstComma = cmd.indexOf(',');
      int secondComma = cmd.indexOf(',', firstComma + 1);
      if (firstComma > 5 && secondComma != -1) {
        userAge = cmd.substring(5, firstComma).toInt();
        userGender = cmd.substring(firstComma + 1, secondComma);
        String hijabStatus = cmd.substring(secondComma + 1);
        userHijab = (hijabStatus.equalsIgnoreCase("true") || hijabStatus.equalsIgnoreCase("1"));
        preferences.begin("data", false);
        preferences.putInt("age", userAge);
        preferences.putString("gender", userGender);
        preferences.putBool("hijab", userHijab);
        preferences.end();
        BTSerial.printf("{\"user_data\":\"saved\",\"age\":%d,\"gender\":\"%s\",\"hijab\":%s}\n", userAge, userGender.c_str(), userHijab ? "true" : "false");
      } else {
        BTSerial.println("{\"error\":\"format salah. Gunakan USER:AGE,GENDER,HIJAB\"}");
      }
    } else if (cmd == "GETIP") {
      if (wifiConnected) {
        BTSerial.printf("{\"ip\":\"%s\"}\n", WiFi.localIP().toString().c_str());
      } else {
        BTSerial.println("{\"error\":\"WiFi belum connect\"}");
      }
    } else if (cmd == "RESET") {
      BTSerial.println("{\"info\":\"ESP32 akan restart\"}");
      delay(500);
      ESP.restart();
    }
  }

  if (millis() - lastWifiStatusSend > 5000) {
    lastWifiStatusSend = millis();
    if (wifiConnected) {
      BTSerial.printf("{\"wifi\":\"connected\",\"ip\":\"%s\"}\n", WiFi.localIP().toString().c_str());
    } else {
      BTSerial.println("{\"wifi\":\"failed\"}");
    }
  }
}