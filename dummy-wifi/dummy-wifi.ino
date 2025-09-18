#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <WebSocketsServer.h>
#include <Preferences.h>
#include <math.h>
#include <ESPmDNS.h>
// ===== Web Server & WebSocket Server =====
WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);
bool wsStarted = false;
const int BUZZER_PIN = 2;
const int LED_BUILTIN = 2;
const int EMERGENCY_BUTTON_PIN = 4;
String wifiSSID = "";
String wifiPASS = "";
bool wifiConnected = false;
String userNIK = "3523072655550012";
Preferences preferences;
volatile bool manualEmergency = false;
volatile unsigned long emergencyStartTime = 0;
const unsigned long emergencyDuration = 5000;
unsigned long lastButtonPress = 0;
const unsigned long debounceDelay = 50;
float manualTemp = 0.0;

void updateBuzzerStatus(float temp) {
  if (temp < 35.0 || manualEmergency) {
    digitalWrite(LED_BUILTIN, HIGH);
  } else {
    digitalWrite(LED_BUILTIN, LOW);
  }
}

void createDummyJSON(char* buf, size_t len) {
  const double baseLat = -7.94621154560041;
  const double baseLon = 112.61544899535379;
  double deltaLat_m = ((double)random(-100, 101));
  double deltaLon_m = ((double)random(-100, 101));
  double lat = baseLat + (deltaLat_m / 111139.0);
  double lon = baseLon + (deltaLon_m / (111320.0 * cos(baseLat * DEG_TO_RAD)));
  float tempL = random(200, 300) / 10.0;
  float tempT = manualTemp;
  bool emergency = manualEmergency;
  updateBuzzerStatus(tempT);
  snprintf(buf, len,
           "{\"lat\":%.6f,\"lon\":%.6f,\"temp_lingkungan\":%.1f,\"temp_tubuh\":%.1f,\"time\":\"2025-07-26 16:00:00\",\"emergency\":%s,\"nik\":\"%s\"}",
           lat, lon, tempL, tempT, emergency ? "true" : "false", userNIK.c_str());
}

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
    case WStype_TEXT: {
      Serial.printf("WebSocket client %u sent text: %s\n", clientNum, payload);
      StaticJsonDocument<200> doc;
      DeserializationError error = deserializeJson(doc, payload);
      if (error) { Serial.println("Failed to parse JSON from WebSocket"); return; }
      const char* command = doc["command"];
      if (command && strcmp(command, "update_user") == 0) {
        Serial.println("Received user data update from app.");
        userNIK = doc["nik"] | userNIK;
        preferences.begin("data", false);
        preferences.putString("nik", userNIK);
        preferences.end();
        Serial.printf("User data updated: NIK=%s\n", userNIK.c_str());
        webSocket.sendTXT(clientNum, "{\"status\":\"user_update_success\"}");
      }
      break;
    }
  }
}

void handleRoot() { server.send(200, "text/plain", "ESP32 WiFi & User Config Portal"); }

void handleWifiConfig() {
  if (server.method() != HTTP_POST) { server.send(405, "application/json", "{\"status\":\"error\",\"message\":\"Method Not Allowed\"}"); return; }
  if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid request\"}"); return; }
  String body = server.arg("plain");
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, body);
  if (error) { server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Failed to parse JSON\"}"); return; }
  const char* ssid = doc["ssid"];
  const char* password = doc["password"];
  if (ssid && password) {
    preferences.begin("data", false);
    preferences.putString("ssid", ssid);
    preferences.putString("pass", password);
    if(!preferences.isKey("nik")){
        preferences.putString("nik", "unknown");
    }
    preferences.end();
    Serial.printf("Received new WiFi credentials: %s\n", ssid);
    server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Configuration received. ESP32 is now attempting to connect to WiFi.\"}");
    delay(100);
    Serial.println("Restarting ESP32 to connect to new WiFi...");
    ESP.restart();
    return;
  }
  server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid data provided\"}");
}

void connectToWiFi(const char* ssid, const char* pass) {
  WiFi.disconnect(true);
  delay(500);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  // PERUBAHAN: Nonaktifkan mode hemat daya WiFi untuk responsivitas maksimum
  WiFi.setSleep(false); 
  
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
    if (!MDNS.begin("esp32-monitor")) { Serial.println("Error setting up MDNS responder!"); } 
    else {
      Serial.println("mDNS responder started. Hostname: esp32-monitor.local");
      MDNS.addService("ws", "tcp", 81);
    }
  } else {
    wifiConnected = false;
    Serial.println("\nWiFi connection failed!");
  }
}

void webSocketTask(void *pvParameters) {
  char buf[256];
  for (;;) {
    if (wsStarted) { webSocket.loop(); }
    if (manualEmergency && (millis() - emergencyStartTime > emergencyDuration)) {
      manualEmergency = false;
      Serial.println("Manual emergency duration ended.");
    }
    if (wifiConnected && wsStarted) {
      static unsigned long lastSend = 0;
      if (millis() - lastSend > 2000) {
        lastSend = millis();
        createDummyJSON(buf, sizeof(buf));
        webSocket.broadcastTXT(buf);
        Serial.println(buf);
      }
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void IRAM_ATTR handleButtonPress() {
  if (millis() - lastButtonPress > debounceDelay) {
    manualEmergency = true;
    emergencyStartTime = millis();
    Serial.println("Manual emergency triggered!");
    lastButtonPress = millis();
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(EMERGENCY_BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(EMERGENCY_BUTTON_PIN), handleButtonPress, FALLING);
  delay(200);

  preferences.begin("data", true);
  wifiSSID = preferences.getString("ssid", "");
  wifiPASS = preferences.getString("pass", "");
  userNIK = preferences.getString("nik", "unknown");
  preferences.end();
  
  if (wifiSSID.length() > 0 && wifiPASS.length() > 0) {
    connectToWiFi(wifiSSID.c_str(), wifiPASS.c_str());
  }

  if (wifiConnected) {
    if (!wsStarted) {
      webSocket.begin();
      webSocket.onEvent(onWebSocketEvent);
      wsStarted = true;
      Serial.println("WebSocket server started on port 81");
    }
  } 
  else {
    WiFi.softAP("ESP32_Config", "12345678");
    Serial.println("AP Started. Connect to SSID: ESP32_Config, PASS: 12345678");
    server.on("/", HTTP_GET, handleRoot);
    server.on("/config", HTTP_POST, handleWifiConfig);
    server.begin();
    Serial.println("HTTP server started");
  }

  xTaskCreate(webSocketTask, "wsTask", 8192, NULL, 1, NULL);
}

// ===== Loop (DENGAN FITUR CLEAR PREFS) =====
void loop() {
  // Cek perintah dari Serial Monitor
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    if (command == "clear_prefs") {
      Serial.println("Perintah diterima. Menghapus data preferences...");
      preferences.begin("data", false);
      preferences.clear();
      preferences.end();
      Serial.println("Preferences dihapus. Me-restart perangkat.");
      delay(100);
      ESP.restart();
    } else if (command.startsWith("set_nik:")) {
      String newNik = command.substring(8);
      if (newNik.length() > 0) {
        userNIK = newNik;
        preferences.begin("data", false);
        preferences.putString("nik", userNIK);
        preferences.end();
        Serial.print("NIK diatur ke: ");
        Serial.println(userNIK);
      } else {
        Serial.println("NIK tidak valid.");
      }
    } else {
      float temp = command.toFloat();
      if (temp > 0.0) {
        manualTemp = temp;
        Serial.print("Suhu tubuh diatur ke: ");
        Serial.println(manualTemp);
      }
    }
  }

  // Hanya jalankan handleClient jika dalam mode AP (belum terhubung ke WiFi)
  if (!wifiConnected) {
    server.handleClient();
  }
  delay(1);
}
