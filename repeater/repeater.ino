#include <Arduino.h>

// ===== PIN DEFINITIONS =====
#define LORA_RX 26
#define LORA_TX 27
#define LORA_BAUD 9600
#define M0 14
#define M1 12
#define LED_BUILTIN 2

// ===== LORA ADDRESSING =====
#define MY_ADDRESS 2       // Alamat untuk Repeater
#define TX_ADDRESS 1       // Alamat untuk node Pengirim (tx2)
#define RX_ADDRESS 3       // Alamat untuk node Penerima (rx2)

HardwareSerial loraSerial(2);

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(M0, OUTPUT);
  pinMode(M1, OUTPUT);
  digitalWrite(M0, LOW);
  digitalWrite(M1, LOW);
  
  loraSerial.begin(LORA_BAUD, SERIAL_8N1, LORA_RX, LORA_TX);
  
  Serial.println("=== LORA REPEATER READY ===");
  Serial.printf("My Address: %d\n", MY_ADDRESS);
}

void loop() {
  static String loraBuffer;

  if (loraSerial.available()) {
    char c = loraSerial.read();
    if (c == '\n') {
      if (loraBuffer.length() > 0) {
        Serial.printf("Pesan diterima: %s\n", loraBuffer.c_str());

        int destAddr, srcAddr;
        char payload[150];

        int parsed = sscanf(loraBuffer.c_str(), "%d,%d,%149[^\n]", &destAddr, &srcAddr, payload);

        if (parsed == 3 && destAddr == MY_ADDRESS) {
          digitalWrite(LED_BUILTIN, HIGH);
          
          String payloadStr = String(payload);
          
          int nextDestAddr;
          if (srcAddr == TX_ADDRESS) {
            nextDestAddr = RX_ADDRESS;
            Serial.printf("Meneruskan pesan dari TX (Addr %d) ke RX (Addr %d)...\n", srcAddr, nextDestAddr);
          } else if (srcAddr == RX_ADDRESS) {
            nextDestAddr = TX_ADDRESS;
            Serial.printf("Meneruskan ACK dari RX (Addr %d) ke TX (Addr %d)...\n", srcAddr, nextDestAddr);
          } else {
            Serial.println("Sumber tidak diketahui, pesan diabaikan.");
            loraBuffer = "";
            digitalWrite(LED_BUILTIN, LOW);
            return;
          }

          String forwardMessage = String(nextDestAddr) + "," + String(MY_ADDRESS) + "," + payloadStr + "\n";
          
          delay(300); // Delay before forwarding to allow direct communication
          loraSerial.print(forwardMessage);
          Serial.printf("Pesan diteruskan: %s", forwardMessage.c_str());
          
          delay(50);
          digitalWrite(LED_BUILTIN, LOW);

        } else {
          Serial.println("Pesan bukan untuk saya atau format salah.");
        }
        loraBuffer = "";
      }
    } else if (c != '\r') {
      loraBuffer += c;
      if (loraBuffer.length() > 200) {
        loraBuffer = "";
      }
    }
  }
}
