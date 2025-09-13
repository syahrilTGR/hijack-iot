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
String lastCompleteJsonPayload = "";

// ================== LoRa Functions ==================
void sendAck(int destinationAddress) {
  String ackMessage = String(destinationAddress) + "," + String(MY_ADDRESS) + ",ACK\n";
  loraSerial.print(ackMessage);
  Serial.printf("[LoRa] ACK dikirim ke Addr %d.\n", destinationAddress);
}

void handlePacket(const String &payload, int sourceAddress) {
  Serial.printf("[LoRa] Paket diterima dari Addr %d: %s\n", sourceAddress, payload.c_str());
  lastAckRecv = millis();
  
  // Balas ACK ke pengirim (repeater atau tx langsung)
  sendAck(sourceAddress);

  // Parsing payload (format CSV)
  float lat, lon, tempLingkungan, tempTubuh;
  char timeStr[30];
  char status[15] = "";
  bool emergencyFlag = false;

  int parsed = sscanf(payload.c_str(), "%f,%f,%f,%f,%29[^,],%14s",
                      &lat, &lon, &tempLingkungan, &tempTubuh, timeStr, status);

  if (parsed >= 5) {
    if (parsed == 6 && String(status) == "EMERGENCY") {
      emergencyFlag = true;
      Serial.println("[LoRa] !!! SINYAL DARURAT DITERIMA DARI TX !!!");
      lastEmergency = millis();
    }

    currentBodyTemp = tempTubuh;

    // Siapkan JSON untuk dikirim ke WebSocket
    StaticJsonDocument<384> outgoingDoc;
    outgoingDoc["lat"] = lat;
    outgoingDoc["lon"] = lon;
    outgoingDoc["temp_lingkungan"] = tempLingkungan;
    outgoingDoc["temp_tubuh"] = tempTubuh;
    outgoingDoc["time"] = timeStr;
    
    bool isEmergencyActive = (millis() - lastEmergency < 10000) || currentBodyTemp < 35.0;
    outgoingDoc["emergency"] = isEmergencyActive;

    outgoingDoc["age"] = userAge;
    outgoingDoc["gender"] = userGender;
    outgoingDoc["hijab"] = userHijab;

    char buffer[384];
    serializeJson(outgoingDoc, buffer);

    lastCompleteJsonPayload = String(buffer);
    webSocket.broadcastTXT(lastCompleteJsonPayload);
    Serial.printf("[WebSocket] Broadcast data: %s\n", buffer);

  } else {
    Serial.println("[LoRa] Format payload tidak valid.");
  }
}

void updateSystemStatus() {
  static uint32_t lastToggle = 0;
  static bool ledState = false;

  // LED indikator LoRa
  if (millis() - lastAckRecv < 4000) {
    if (millis() - lastToggle >= 1000) {
      ledState = !ledState;
      digitalWrite(LED_BUILTIN, ledState);
      lastToggle = millis();
    }
  } else {
    digitalWrite(LED_BUILTIN, LOW);
  }

  // Buzzer darurat
  bool isEmergencyActive = (millis() - lastEmergency < 10000) || currentBodyTemp < 35.0;
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

        // Parsing format: DEST,SRC,PAYLOAD
        int parsed = sscanf(loraBuffer.c_str(), "%d,%d,%199[^\\n]", &destAddr, &srcAddr, payload);

        if (parsed == 3 && (destAddr == MY_ADDRESS || srcAddr == TX_ADDRESS)) {
          String payloadStr = String(payload);
          if (payloadStr == "ACK") {
            // Jika ACK dari Repeater (balasan untuk data kita atau ping yg diteruskan)
            if (srcAddr == REPEATER_ADDRESS) {
                Serial.printf("[LoRa] Terima ACK/ping dari Repeater (Addr %d)\n", srcAddr);
                lastAckRecv = millis();
                sendAck(srcAddr);
            } 
            // Jika "menguping" ping asli dari TX ke Repeater
            else if (srcAddr == TX_ADDRESS) {
                Serial.printf("[LoRa] Dengar ping langsung dari Pengirim (Addr %d)\n", srcAddr);
                // Tidak melakukan apa-apa, hanya logging untuk debug.
            }
          } else {
            // Ini adalah paket data, proses isinya
            handlePacket(payloadStr, srcAddr);
          }
        } else {
          // Bukan untuk alamat ini atau format salah
          Serial.printf("[LoRa] Pesan diabaikan: %s\n", loraBuffer.c_str());
        }
        loraBuffer = ""; // Kosongkan buffer
      }
    } else if (c != '\r') {
      loraBuffer += c;
      if (loraBuffer.length() > 300) loraBuffer = "";
    }
  }

  updateSystemStatus();
  delay(1);
}
