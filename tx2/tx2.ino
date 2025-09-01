#define EMERGENCY_BTN 15
#define LED_BUILTIN 2

#define LORA_RX 26
#define LORA_TX 27
#define LORA_BAUD 9600
#define LORA_M0 14
#define LORA_M1 12

HardwareSerial loraSerial(1);

const uint32_t SEND_INTERVAL = 5000;
const uint32_t PING_INTERVAL = 2000;

uint32_t lastSend = 0;
uint32_t lastPing = 0;
uint32_t lastAckTime = 0;

bool lastButtonState = HIGH;
bool emergencyActive = false;
uint32_t emergencyStart = 0;   // waktu tombol ditekan

// ===== Create JSON Dummy (tanpa emergency) =====
String createDummyJSON() {
  String json = "{";
  json += "\"lat\": -6.234567,";
  json += "\"lon\": 106.987654,";
  json += "\"temp_lingkungan\": " + String(random(200, 300) / 10.0, 1) + ",";
  json += "\"temp_tubuh\": " + String(random(350, 380) / 10.0, 1) + ",";
  json += "\"time\": \"2025-07-26 16:00:00\"";
  json += "}"; 
  return json;
}

// === Bungkus dengan emergency dari tombol ===
String buildPayload(bool emergency) {
  String json = createDummyJSON();
  json.remove(json.length() - 1); // hapus "}"
  json += ",\"emergency\": " + String(emergency ? "true" : "false") + "}";
  return json;
}

void sendPacket(bool emergency) {
  String payload = buildPayload(emergency);
  payload += "\n";
  loraSerial.print(payload);
  Serial.print("[TX] Kirim Data: ");
  Serial.println(payload);
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
      if (buffer.length() > 200) buffer = "";
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

void setup() {
  Serial.begin(115200);

  pinMode(LORA_M0, OUTPUT);
  pinMode(LORA_M1, OUTPUT);
  digitalWrite(LORA_M0, LOW);
  digitalWrite(LORA_M1, LOW);
  loraSerial.begin(LORA_BAUD, SERIAL_8N1, LORA_RX, LORA_TX);

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(EMERGENCY_BTN, INPUT_PULLUP);

  Serial.println("=== NODE PENDAKI (TX) - DEMO DUMMY JSON + BUTTON ===");
}

void loop() {
  // cek tombol darurat
  bool buttonState = digitalRead(EMERGENCY_BTN);
  if (lastButtonState == HIGH && buttonState == LOW) {
    Serial.println("!!! TOMBOL DARURAT DITEKAN !!!");
    emergencyActive = true;
    emergencyStart = millis();   // catat waktu mulai emergency
    sendPacket(true);            // langsung kirim sekali
  }
  lastButtonState = buttonState;

  // reset emergency setelah 2 periode (10 detik)
  if (emergencyActive && millis() - emergencyStart > 2 * SEND_INTERVAL) {
    emergencyActive = false;
    Serial.println("[TX] Emergency otomatis reset (selesai 2 periode)");
  }

  // kirim data rutin (dummy + status emergency saat ini)
  if (millis() - lastSend >= SEND_INTERVAL) {
    sendPacket(emergencyActive);
    lastSend = millis();
  }

  // kirim ping rutin
  if (millis() - lastPing >= PING_INTERVAL) {
    sendAckPing();
    lastPing = millis();
  }

  // cek pesan masuk
  handleIncoming();

  // update LED
  updateLED();
}
