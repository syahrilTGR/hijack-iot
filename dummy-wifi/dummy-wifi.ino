#include <WiFi.h>
#include <WebSocketsServer.h>

// ===== WiFi AP Config =====
const char* ssid = "ESP32_Hotspot";
const char* password = "12345678";

// ===== WebSocket Server on Port 81 =====
WebSocketsServer webSocket = WebSocketsServer(81);

// ===== Create JSON Dummy =====
String createDummyJSON() {
  String json = "{";
  json += "\"lat\": -6.234567,";
  json += "\"lon\": 106.987654,";
  json += "\"temp_lingkungan\": " + String(random(200, 300) / 10.0, 1) + ",";
  json += "\"temp_tubuh\": " + String(random(350, 380) / 10.0, 1) + ",";
  json += "\"time\": \"2025-07-26 16:00:00\",";
  json += "\"emergency\": " + String(random(0, 10) > 7 ? "true" : "false");
  json += "}";
  return json;
}

// ===== WebSocket Events =====
void onWebSocketEvent(uint8_t clientNum, WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED: {
      Serial.printf("Client %u connected\n", clientNum);
      String data = createDummyJSON();
      webSocket.sendTXT(clientNum, data);
      break;
    }
    case WStype_DISCONNECTED:
      Serial.printf("Client %u disconnected\n", clientNum);
      break;
    case WStype_TEXT:
      Serial.printf("Client %u sent text: %s\n", clientNum, payload);
      break;
  }
}

void setup() {
  Serial.begin(115200);

  // Start WiFi Access Point
  WiFi.softAP(ssid, password);
  Serial.println("WiFi AP started");
  Serial.print("IP Address: ");
  Serial.println(WiFi.softAPIP()); // biasanya 192.168.4.1

  // Start WebSocket Server
  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);

  Serial.println("WebSocket server started on port 81");
}

void loop() {
  webSocket.loop();

  // Broadcast every 2 seconds
  static unsigned long lastSend = 0;
  if (millis() - lastSend > 2000) {
    lastSend = millis();
    String data = createDummyJSON();
    webSocket.broadcastTXT(data);
    Serial.println("Broadcast: " + data);
  }
}