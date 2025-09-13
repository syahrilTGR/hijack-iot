#include <Arduino.h>

// ===== PIN DEFINITIONS =====
#define LORA_RX 16
#define LORA_TX 17
#define LORA_BAUD 9600
#define LORA_M0 4
#define LORA_M1 5
#define LED_BUILTIN 2

// ===== LORA ADDRESSING =====
#define MY_ADDRESS 2       // Alamat untuk Repeater
#define TX_ADDRESS 1       // Alamat untuk node Pengirim (Tes1)
#define RX_ADDRESS 3       // Alamat untuk node Penerima (rx2)

HardwareSerial loraSerial(2);

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);

  // Setup LoRa E220
  pinMode(LORA_M0, OUTPUT);
  pinMode(LORA_M1, OUTPUT);
  digitalWrite(LORA_M0, LOW); // Mode Transparant
  digitalWrite(LORA_M1, LOW);
  
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

        // Parsing format: DEST,SRC,PAYLOAD
        int parsed = sscanf(loraBuffer.c_str(), "%d,%d,%149[^
]", &destAddr, &srcAddr, payload);

        if (parsed == 3 && destAddr == MY_ADDRESS) {
          digitalWrite(LED_BUILTIN, HIGH); // Nyalakan LED saat bekerja
          
          String payloadStr = String(payload);
          
          // Tentukan tujuan selanjutnya
          int nextDestAddr;
          if (srcAddr == TX_ADDRESS) {
            // Jika pesan dari TX, teruskan ke RX
            nextDestAddr = RX_ADDRESS;
            Serial.printf("Meneruskan pesan dari TX (Addr %d) ke RX (Addr %d)...
", srcAddr, nextDestAddr);
          } else if (srcAddr == RX_ADDRESS) {
            // Jika pesan (kemungkinan ACK) dari RX, teruskan ke TX
            nextDestAddr = TX_ADDRESS;
            Serial.printf("Meneruskan ACK dari RX (Addr %d) ke TX (Addr %d)...
", srcAddr, nextDestAddr);
          } else {
            Serial.println("Sumber tidak diketahui, pesan diabaikan.");
            loraBuffer = "";
            digitalWrite(LED_BUILTIN, LOW);
            return; // Keluar dari blok if
          }

          // Buat pesan baru untuk diteruskan
          String forwardMessage = String(nextDestAddr) + "," + String(MY_ADDRESS) + "," + payloadStr + "\n";
          
          // Kirim pesan yang sudah di-format ulang
          loraSerial.print(forwardMessage);
          Serial.printf("Pesan diteruskan: %s", forwardMessage.c_str());
          
          delay(50); // Beri jeda singkat
          digitalWrite(LED_BUILTIN, LOW); // Matikan LED setelah selesai

        } else {
          Serial.println("Pesan bukan untuk saya atau format salah.");
        }
        loraBuffer = ""; // Kosongkan buffer
      }
    } else if (c != '\r') {
      loraBuffer += c;
      if (loraBuffer.length() > 200) {
        loraBuffer = ""; // Hindari buffer overflow
      }
    }
  }
}
