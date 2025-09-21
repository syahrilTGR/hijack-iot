#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <WebSocketsServer.h>
#include <Preferences.h>
#include <math.h>
#include <ESPmDNS.h>

// ===== LORA ADDRESSING =====
#define MY_ADDRESS 3
#define REPEATER_ADDRESS 2
#define TX_ADDRESS 1

// ===== Pin Definitions =====
const int BUZZER_PIN = 13;
#define LORA_RX 16
#define LORA_TX 17
#define LORA_BAUD 9600
#define LORA_M0 4
#define LORA_M1 5
const int LED_BUILTIN = 2;

// ===== Web Server & WebSocket Server =====
WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);
bool wsStarted = false;

// ===== LoRa =====
HardwareSerial loraSerial(2);
uint32_t lastAckRecv = 0;
uint32_t lastEmergency = 0;

// ===== Status & Konfigurasi =====
String wifiSSID = "";
String wifiPASS = "";
bool wifiConnected = false;
int userAge = 0;
String userGender = "unknown";
bool userHijab = false;
Preferences preferences;
float currentBodyTemp = 36.5;
String lastNikReceived = "0";
String lastCompleteJsonPayload = "";

// ================== LoRa Functions ==================
void sendAck(int destinationAddress) {
  String ackMessage = String(destinationAddress) + "," + String(MY_ADDRESS) + ",ACK\n";
  loraSerial.print(ackMessage);
  Serial.printf("[LoRa] ACK dikirim ke Addr %d.\n", destinationAddress);
}

void handlePacket(const String &payload, int sourceAddress) {
  Serial.printf("[LoRa] Paket DATA diterima dari Addr %d: %s\n", sourceAddress, payload.c_str());
  lastAckRecv = millis();
  
  sendAck(sourceAddress);

  // Flexible CSV Parsing
  float lat, lon, tempLingkungan, tempTubuh;
  String timeStr, nik, status;
  bool emergencyFlag = false;

  String parts[8];
  int partCount = 0;
  int lastIndex = 0;
  for (int i = 0; i < payload.length(); i++) {
    if (payload.charAt(i) == ',') {
      if (partCount < 7) { // Prevent overflow
        parts[partCount++] = payload.substring(lastIndex, i);
        lastIndex = i + 1;
      }
    }
  }
  parts[partCount++] = payload.substring(lastIndex);

  if (partCount >= 6) { // Must have at least LAT,LON,T_ENV,T_BODY,TIME,NIK
    lat = parts[0].toFloat();
    lon = parts[1].toFloat();
    tempLingkungan = parts[2].toFloat();
    tempTubuh = parts[3].toFloat();
    timeStr = parts[4];
    nik = parts[5];
    lastNikReceived = nik;

    if (partCount > 6 && parts[6] == "EMERGENCY") {
      emergencyFlag = true;
      Serial.println("[LoRa] !!! SINYAL DARURAT DITERIMA DARI TX !!!");
      lastEmergency = millis();
    }

    currentBodyTemp = tempTubuh;
    Serial.printf("[LoRa] NIK Diterima: %s\n", nik.c_str());

    // Prepare JSON for WebSocket
    StaticJsonDocument<512> outgoingDoc;
    outgoingDoc["lat"] = lat;
    outgoingDoc["lon"] = lon;
    outgoingDoc["temp_lingkungan"] = tempLingkungan;
    outgoingDoc["temp_tubuh"] = tempTubuh;
    outgoingDoc["time"] = timeStr;
    outgoingDoc["nik"] = nik;
    
    bool isEmergencyActive = (millis() - lastEmergency < 10000);
    outgoingDoc["emergency"] = isEmergencyActive;

    outgoingDoc["age"] = userAge;
    outgoingDoc["gender"] = userGender;
    outgoingDoc["hijab"] = userHijab;

    char buffer[512];
    serializeJson(outgoingDoc, buffer);

    lastCompleteJsonPayload = String(buffer);
    webSocket.broadcastTXT(lastCompleteJsonPayload);
    Serial.printf("[WebSocket] Broadcast data: %s\n", buffer);

  } else {
    Serial.println("[LoRa] Format payload data tidak valid.");
  }
}

void updateSystemStatus() {
  static uint32_t lastToggle = 0;
  static bool ledState = false;

  // LED indicator for LoRa activity
  if (millis() - lastAckRecv < 4000) {
    if (millis() - lastToggle >= 1000) {
      ledState = !ledState;
      digitalWrite(LED_BUILTIN, ledState);
      lastToggle = millis();
    }
  } else {
    digitalWrite(LED_BUILTIN, LOW);
  }

  // Emergency buzzer
  bool isEmergencyActive = (millis() - lastEmergency < 10000);
  if (isEmergencyActive) {
    digitalWrite(BUZZER_PIN, HIGH);
  } else {
    digitalWrite(BUZZER_PIN, LOW);
  }
}

// ================== WiFi / User Config / WebSocket ==================
void handleRoot() { 
  server.send(200, "text/plain", "ESP32 WiFi & User Config Portal"); 
}

void handleWifiConfig() {
  if (server.method() != HTTP_POST) { 
    server.send(405, "application/json", "{\"status\":\"error\",\"message\":\"Method Not Allowed\"}"); 
    return; 
  }
  if (!server.hasArg("plain")) { 
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid request\"}"); 
    return; 
  }

  String body = server.arg("plain");
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, body);
  if (error) { 
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Failed to parse JSON\"}"); 
    return; 
  }

  const char* ssid = doc["ssid"];
  const char* password = doc["password"];
  if (ssid && password) {
    preferences.begin("data", false);
    preferences.putString("ssid", ssid);
    preferences.putString("pass", password);
    if(!preferences.isKey("age")){
        preferences.putInt("age", 0);
        preferences.putString("gender", "unknown");
        preferences.putBool("hijab", false);
    }
    preferences.end();

    Serial.printf("Received new WiFi credentials: %s\n", ssid);
    server.send(200, "application/json", 
                "{\"status\":\"success\",\"message\":\"Configuration received. ESP32 is now attempting to connect to WiFi.\"}");
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
    if (!MDNS.begin("esp32-monitor")) { 
      Serial.println("Error setting up MDNS responder!"); 
    } else {
      Serial.println("mDNS responder started. Hostname: esp32-monitor.local");
      MDNS.addService("ws", "tcp", 81);
    }
  } else {
    wifiConnected = false;
    Serial.println("\nWiFi connection failed!");
  }
}

void onWebSocketEvent(uint8_t clientNum, WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      Serial.printf("WebSocket client %u connected\n", clientNum);
      if (lastCompleteJsonPayload.length() > 0) {
        webSocket.sendTXT(clientNum, lastCompleteJsonPayload);
      } else {
        webSocket.sendTXT(clientNum, "{\"status\":\"waiting_for_data\"}");
      }
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
        userAge = doc["age"] | userAge;
        userGender = doc["gender"] | userGender;
        userHijab = doc["hijab"] | userHijab;
        preferences.begin("data", false);
        preferences.putInt("age", userAge);
        preferences.putString("gender", userGender);
        preferences.putBool("hijab", userHijab);
        preferences.end();
        Serial.printf("User data updated: Age=%d, Gender=%s, Hijab=%s\n", 
                      userAge, userGender.c_str(), userHijab ? "true" : "false");
        webSocket.sendTXT(clientNum, "{\"status\":\"user_update_success\"}");
      }
      break;
    }
  }
}

void webSocketTask(void *pvParameters) {
  for (;;) {
    if (wsStarted) { 
      webSocket.loop(); 
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

// ================== Setup ==================
void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  
  pinMode(LORA_M0, OUTPUT);
  pinMode(LORA_M1, OUTPUT);
  digitalWrite(LORA_M0, LOW);
  digitalWrite(LORA_M1, LOW);
  loraSerial.begin(LORA_BAUD, SERIAL_8N1, LORA_RX, LORA_TX);
  Serial.println("=== LoRa Receiver Ready ===");

  delay(200);

  // Load saved config
  preferences.begin("data", true);
  wifiSSID = preferences.getString("ssid", "");
  wifiPASS = preferences.getString("pass", "");
  userAge = preferences.getInt("age", 0);
  userGender = preferences.getString("gender", "unknown");
  userHijab = preferences.getBool("hijab", false);
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

// ================== Loop ==================
void loop() {
  // Perintah clear prefs
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
    }
  }

  // AP mode
  if (!wifiConnected) {
    server.handleClient();
  }

  // LoRa handling
  static String loraBuffer;
  while (loraSerial.available()) {
    char c = loraSerial.read();
    if (c == '\n') {
      if (loraBuffer.length() > 0) {
        
        int destAddr, srcAddr;
        char payload[200];

        int parsed = sscanf(loraBuffer.c_str(), "%d,%d,%199[^\n]", &destAddr, &srcAddr, payload);

        if (parsed == 3 && (destAddr == MY_ADDRESS || destAddr == REPEATER_ADDRESS)) {
          String payloadStr = String(payload);

          if (payloadStr.startsWith("ACK")) {
            int commaIndex = payloadStr.indexOf(',');
            if (commaIndex != -1) {
              lastNikReceived = payloadStr.substring(commaIndex + 1);
              Serial.printf("[LoRa] NIK %s dari PING.\n", lastNikReceived.c_str());
            }
            
            if (srcAddr == REPEATER_ADDRESS) {
                Serial.printf("[LoRa] Terima ACK dari Repeater (Addr %d)\n", srcAddr);
                lastAckRecv = millis();
            } 
            else if (srcAddr == TX_ADDRESS) {
                Serial.printf("[LoRa] Dengar PING langsung dari Pengirim (Addr %d)\n", srcAddr);
                lastAckRecv = millis();
                sendAck(srcAddr);
            }
          } else {
            handlePacket(payloadStr, srcAddr);
          }
        } else {
          Serial.printf("[LoRa] Pesan diabaikan: %s\n", loraBuffer.c_str());
        }
        loraBuffer = "";
      }
    } else if (c != '\r') {
      loraBuffer += c;
      if (loraBuffer.length() > 300) loraBuffer = "";
    }
  }

  updateSystemStatus();
  delay(1);
}
