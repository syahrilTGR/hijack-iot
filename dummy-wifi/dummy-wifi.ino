#include <Arduino.h>
#include <BluetoothSerial.h>
#include <WiFi.h>
#include <WebSocketsServer.h>
#include <Preferences.h>
#include <math.h>

BluetoothSerial BTSerial;
Preferences preferences;

// ===== WebSocket Server =====
WebSocketsServer webSocket = WebSocketsServer(81);
bool wsStarted = false;

// ===== WiFi Credentials =====
String wifiSSID = "";
String wifiPASS = "";
bool wifiConnected = false;

// ===== Dummy JSON dengan koordinat ±100m =====
void createDummyJSON(char* buf, size_t len) {
  const double baseLat = -7.94621154560041;
  const double baseLon = 112.61544899535379;

  // Geser ±100 meter random
  double deltaLat = ((random(-100, 101)) / 1000.0) / 111.0; // ±0.1 km → ±100 m
  double deltaLon = ((random(-100, 101)) / 1000.0) / (111.0 * cos(baseLat * DEG_TO_RAD));

  double lat = baseLat + deltaLat;
  double lon = baseLon + deltaLon;

  float tempL = random(200, 300) / 10.0;
  float tempT = random(350, 380) / 10.0;
  bool emergency = (random(0, 10) > 7);

  snprintf(buf, len,
           "{\"lat\":%.6f,\"lon\":%.6f,\"temp_lingkungan\":%.1f,\"temp_tubuh\":%.1f,\"time\":\"2025-07-26 16:00:00\",\"emergency\":%s}",
           lat, lon, tempL, tempT, emergency ? "true" : "false");
}

// ===== WebSocket Events =====
void onWebSocketEvent(uint8_t clientNum, WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      Serial.printf("WebSocket client %u connected\n", clientNum);
      char buf[128];
      createDummyJSON(buf, sizeof(buf));
      {
        String data(buf);
        webSocket.sendTXT(clientNum, data);
      }
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
  WiFi.disconnect(true); // Pastikan WiFi disconnect dulu
  delay(500);            // Beri jeda agar proses disconnect selesai
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
  char buf[128];
  for (;;) {
    if (wifiConnected && wsStarted) {
      webSocket.loop();
      static unsigned long lastSend = 0;
      if (millis() - lastSend > 2000) {
        lastSend = millis();
        createDummyJSON(buf, sizeof(buf));
        String data(buf);
        webSocket.broadcastTXT(data);
        Serial.println("Broadcast: " + data);
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
  delay(200); // beri waktu task BT siap

  Serial.println("=== Bluetooth ready for WiFi input ===");
  BTSerial.println("{\"info\":\"Ketik WIFI:SSID,PASS untuk konek WiFi atau GETIP untuk IP\"}");

  // Load SSID & Password dari Preferences
  preferences.begin("wifi", false);
  wifiSSID = preferences.getString("ssid", "");
  wifiPASS = preferences.getString("pass", "");
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

  // Start WebSocket task (loop)
  xTaskCreate(webSocketTask, "wsTask", 4096, NULL, 1, NULL);
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
        Serial.printf("Received WiFi credentials via BT: %s / %s\n", wifiSSID.c_str(), wifiPASS.c_str());

        // Simpan ke Preferences
        preferences.begin("wifi", false);
        preferences.putString("ssid", wifiSSID);
        preferences.putString("pass", wifiPASS);
        preferences.end();
        Serial.println("SSID & Password tersimpan di Preferences");

        connectToWiFi(wifiSSID.c_str(), wifiPASS.c_str()); // Sudah otomatis disconnect dulu
        if (wifiConnected && !wsStarted) {
          webSocket.begin();
          webSocket.onEvent(onWebSocketEvent);
          wsStarted = true;
          Serial.println("WebSocket server started on port 81");
        }
      } else {
        BTSerial.println("{\"error\":\"format salah. Gunakan WIFI:SSID,PASS\"}");
      }
    } else if (cmd == "GETIP") {
      if (wifiConnected) {
        BTSerial.printf("{\"ip\":\"%s\"}\n", WiFi.localIP().toString().c_str());
        Serial.printf("Sent IP via BT: %s\n", WiFi.localIP().toString().c_str());
      } else {
        BTSerial.println("{\"error\":\"WiFi belum connect\"}");
        Serial.println("GETIP requested but WiFi not connected");
      }
    }
    else if (cmd == "RESET") {
      BTSerial.println("{\"info\":\"ESP32 akan restart\"}");
      delay(500);
      ESP.restart();
    }
  }

  // Kirim status WiFi setiap 5 detik
  if (millis() - lastWifiStatusSend > 5000) {
    lastWifiStatusSend = millis();
    if (wifiConnected) {
      BTSerial.printf("{\"wifi\":\"connected\",\"ip\":\"%s\"}\n", WiFi.localIP().toString().c_str());
    } else {
      BTSerial.println("{\"wifi\":\"failed\"}");
    }
  }
}