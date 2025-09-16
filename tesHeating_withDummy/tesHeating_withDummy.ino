#include <BluetoothSerial.h>

// ===== LORA ADDRESSING =====
#define MY_ADDRESS 1
#define REPEATER_ADDRESS 2

// ===== PIN DEFINITIONS =====
#define EMERGENCY_BTN 15
#define BUZZER_PIN 25
#define HEATER_PIN 23
#define LED_BUILTIN 2

// ===== LORA DEFINITIONS =====
#define LORA_RX 26
#define LORA_TX 27
#define LORA_BAUD 9600
#define LORA_M0 14
#define LORA_M1 12

// ===== OBJECTS =====
HardwareSerial loraSerial(1);
BluetoothSerial BTSerial;

// ===== TIMING & STATE =====
const uint32_t SEND_INTERVAL = 5000;
const uint32_t PING_INTERVAL = 2000;
const uint32_t EMERGENCY_TIMEOUT = 30000; // 30 detik
uint32_t lastSend = 0;
uint32_t lastPing = 0;
uint32_t lastAckTime = 0;
bool lastButtonState = HIGH;

// --- Buzzer state ---
bool buzzerActive = false;
uint32_t buzzerStartTime = 0;
const uint32_t BUZZER_DURATION = 1000; // 1 detik

// --- Emergency state ---
bool emergencyActive = false;
uint32_t emergencyStart = 0;

// --- Heater state ---
int heaterPowerPercentage = 0;
bool heaterForcedOn = false; // True jika heater dihidupkan paksa oleh sistem (misal severe hypo)

// ===== HYPOTHERMIA LOGIC =====
const float TEMP_NORMAL_THRESHOLD = 36.0; // Suhu di atas ini dianggap normal
const float TEMP_MILD_HYPO_THRESHOLD = 32.0; // Suhu di bawah ini dianggap severe hypo

enum HypoState {
  NORMAL,
  MILD_HYPO_WARNING,    // Suhu 32-36, peringatan aktif
  MILD_HYPO_IGNORED,    // Suhu 32-36, peringatan diabaikan (countdown aktif)
  SEVERE_HYPO,          // Suhu <= 32
  RECOVERING            // Status transisi setelah pulih dari severe
};
HypoState currentHypoState = NORMAL;

uint32_t ignoreCountdownStartTime = 0;
const uint32_t IGNORE_DURATION_MS = 15 * 60 * 1000; // 15 menit

// --- Stabilization Timer ---
uint32_t stabilizationStartTime = 0;
const uint32_t STABILIZATION_DURATION_MS = 5 * 60 * 1000; // 5 menit

uint32_t lastHypoNotificationTime = 0;
const uint32_t HYPO_NOTIFICATION_COOLDOWN_MS = 60000; // Cooldown 1 menit untuk notifikasi/buzzer

// --- Recovery Detection (Severe to Mild) ---
const uint32_t RECOVERY_WINDOW_MS = 20 * 1000; // 20 detik
const int TEMP_HISTORY_SIZE = 20; // Menyimpan 20 sampel suhu dalam 20 detik (1 sampel/detik)
float tempHistory[TEMP_HISTORY_SIZE];
int tempHistoryIndex = 0;
uint32_t lastTempSampleTime = 0;
const uint32_t TEMP_SAMPLE_INTERVAL_MS = 1000; // Ambil sampel setiap 1 detik

// =================================
// BLUETOOTH & HEATER FUNCTIONS
// =================================

void setHeaterPower(int percentage, const char* reason = "command") {
  heaterPowerPercentage = constrain(percentage, 0, 100);
  int dutyCycle = map(heaterPowerPercentage, 0, 100, 0, 255);
  analogWrite(HEATER_PIN, dutyCycle); // Menggunakan analogWrite
  
  Serial.printf("[HEATER] Power diatur ke %d%% (Alasan: %s)\n", heaterPowerPercentage, reason);

  // Kirim update ke aplikasi via Bluetooth
  BTSerial.printf("{\"event\":\"power_update\",\"power\":%d,\"reason\":\"%s\"}\n", heaterPowerPercentage, reason);
}

void sendHypoNotification(float temp) {
  if (millis() - lastHypoNotificationTime > HYPO_NOTIFICATION_COOLDOWN_MS) {
    digitalWrite(BUZZER_PIN, HIGH);
    buzzerActive = true;
    buzzerStartTime = millis();
    Serial.printf("[NOTIF] Suhu tubuh %.2f C. Mengirim peringatan hipotermia ke HP.\n", temp);
    BTSerial.printf("{\"event\":\"hypothermia_warning\",\"temperature\":%.2f,\"state\":\"%s\"}\n", temp, 
                    currentHypoState == MILD_HYPO_WARNING ? "mild" : "severe");
    lastHypoNotificationTime = millis();
  }
}

void handleBluetooth() {
  if (BTSerial.available()) {
    String cmd = BTSerial.readStringUntil('\n');
    cmd.trim();
    Serial.printf("[BT] Perintah diterima: %s\n", cmd.c_str());

    if (cmd.startsWith("PWM:")) {
      int power = cmd.substring(4).toInt();
      // Hanya izinkan aplikasi mengatur PWM jika tidak dalam severe hypo atau emergency
      if (!heaterForcedOn && !emergencyActive) {
        setHeaterPower(power, "app_command");
      } else {
        BTSerial.println("{\"event\":\"error\",\"message\":\"heater_override_active\"}");
      }
    } else if (cmd == "GET_STATUS") {
      String hypoStateStr;
      switch (currentHypoState) {
        case NORMAL: hypoStateStr = "normal"; break;
        case MILD_HYPO_WARNING: hypoStateStr = "mild_warning"; break;
        case MILD_HYPO_IGNORED: hypoStateStr = "mild_ignored"; break;
        case SEVERE_HYPO: hypoStateStr = "severe"; break;
        case RECOVERING: hypoStateStr = "recovering"; break;
      }
      BTSerial.printf("{\"event\":\"status_report\",\"power\":%d,\"emergency\":%s,\"hypo_state\":\"%s\",\"temp_history_count\":%d}\n", 
                      heaterPowerPercentage, emergencyActive ? "true" : "false", hypoStateStr.c_str(), tempHistoryIndex);
    } else if (cmd == "ACTIVATE_HEATER") {
      if (currentHypoState == MILD_HYPO_WARNING || currentHypoState == MILD_HYPO_IGNORED) {
        setHeaterPower(100, "app_activate");
        heaterForcedOn = true; // Aplikasi mengaktifkan, jadi dianggap forced
        currentHypoState = NORMAL; // Kembali ke normal setelah diaktifkan
        Serial.println("[BT] Aplikasi mengaktifkan pemanas. Status kembali NORMAL.");
      } else {
        BTSerial.println("{\"event\":\"error\",\"message\":\"not_in_mild_hypo\"}");
      }
    } else if (cmd.startsWith("IGNORE_WARNING:")) {
      if (currentHypoState == MILD_HYPO_WARNING) {
        int durationMinutes = cmd.substring(15).toInt(); // Misal "IGNORE_WARNING:15"
        if (durationMinutes > 0) {
          ignoreCountdownStartTime = millis();
          // IGNORE_DURATION_MS = durationMinutes * 60 * 1000; // Ini akan mengubah const, tidak boleh.
          // Untuk saat ini, kita pakai IGNORE_DURATION_MS yang sudah didefinisikan.
          currentHypoState = MILD_HYPO_IGNORED;
          Serial.printf("[BT] Peringatan diabaikan selama %d menit.\n", durationMinutes);
          BTSerial.printf("{\"event\":\"warning_ignored\",\"duration_minutes\":%d}\n", durationMinutes);
        } else {
          BTSerial.println("{\"event\":\"error\",\"message\":\"invalid_ignore_duration\"}");
        }
      } else {
        BTSerial.println("{\"event\":\"error\",\"message\":\"not_in_mild_hypo_warning\"}");
      }
    }
    else {
      BTSerial.println("{\"event\":\"error\",\"message\":\"unknown_command\"}");
    }
  }
}

// =================================
// LORA & DATA FUNCTIONS
// =================================

String buildLocalTime() {
    return "2025-01-01 12:00:00"; // Dummy time for dummy mode
}

String getHypoStateString(HypoState state) {
  switch (state) {
    case NORMAL: return "NORMAL";
    case MILD_HYPO_WARNING: return "MILD_WARN";
    case MILD_HYPO_IGNORED: return "MILD_IGN";
    case SEVERE_HYPO: return "SEVERE";
    case RECOVERING: return "RECOVERING";
    default: return "UNKNOWN";
  }
}

void sendPacket(float tempLingkungan, float tempTubuh, bool emergency = false) {
  String payload;
  String header = String(REPEATER_ADDRESS) + "," + String(MY_ADDRESS) + ",";

  payload = "0.000000,0.000000," + // Dummy GPS
            String(tempLingkungan, 2) + "," +
            String(tempTubuh, 2) + "," +
            buildLocalTime() + "," +
            getHypoStateString(currentHypoState); // Tambahkan status hipotermia

  if (emergency) payload += ",EMERGENCY";

  String fullMessage = header + payload + "\n";
  loraSerial.print(fullMessage);
  Serial.print("Kirim Data: ");
  Serial.print(fullMessage);
}

void sendAckPing() {
  String pingMessage = String(REPEATER_ADDRESS) + "," + String(MY_ADDRESS) + ",ACK\n";
  loraSerial.print(pingMessage);
  Serial.println("[TX] ACK/Ping dikirim ke Repeater");
}

void handleLoRaIncoming() {
  static String buffer;
  while (loraSerial.available()) {
    char c = loraSerial.read();
    if (c == '\n') {
      int destAddr, srcAddr;
      char payload[20];
      sscanf(buffer.c_str(), "%d,%d,%19s", &destAddr, &srcAddr, payload);

      if (destAddr == MY_ADDRESS && String(payload) == "ACK") {
        lastAckTime = millis();
        Serial.printf("[TX] ACK diterima dari Addr %d\n", srcAddr);
      }
      buffer = "";
    } else if (c != '\r') {
      buffer += c;
      if (buffer.length() > 50) buffer = "";
    }
  }
}

// =================================
// UTILITY FUNCTIONS
// =================================

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

void updateBuzzer() {
  if (buzzerActive && millis() - buzzerStartTime >= BUZZER_DURATION) {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerActive = false;
  }
}

// =================================
// SETUP & LOOP
// =================================

void setup() {
  Serial.begin(115200);
  
  // Mulai Bluetooth
  BTSerial.begin("ESP32-Heater-Dummy");
  Serial.println("Perangkat Bluetooth siap.");

  // Konfigurasi LoRa
  pinMode(LORA_M0, OUTPUT);
  pinMode(LORA_M1, OUTPUT);
  digitalWrite(LORA_M0, LOW);
  digitalWrite(LORA_M1, LOW);
  loraSerial.begin(LORA_BAUD, SERIAL_8N1, LORA_RX, LORA_TX);

  // Konfigurasi Pin-pin
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(EMERGENCY_BTN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(HEATER_PIN, OUTPUT); // Atur pin heater sebagai output
  // ledcSetup(0, 5000, 8); // Channel 0, 5 kHz, 8 bit resolution - DIHAPUS
  // ledcAttachPin(HEATER_PIN, 0); // Attach the pin to the channel - DIHAPUS

  // Atur daya heater ke 0 saat awal
  setHeaterPower(0, "init");

  // Inisialisasi riwayat suhu
  for (int i = 0; i < TEMP_HISTORY_SIZE; i++) {
    tempHistory[i] = 0.0;
  }

  Serial.println("=== NODE TX - HEATER DUMMY MODE ===");
  BTSerial.println("{\"event\":\"ready\"}");
}

void loop() {
  // 1. Handle input dari Bluetooth
  handleBluetooth();

  // 2. Generate data sensor dummy
  // Untuk pengujian, kita bisa membuat suhu dummy berubah-ubah
  static float dummyTempTubuh = 37.0;
  static uint32_t lastDummyTempChange = 0;
  const uint32_t DUMMY_TEMP_CHANGE_INTERVAL = 5000; // Ganti suhu setiap 5 detik

  if (millis() - lastDummyTempChange > DUMMY_TEMP_CHANGE_INTERVAL) {
    // Simulasikan penurunan suhu secara bertahap
    if (random(0, 10) < 3) { // 30% kemungkinan suhu turun
      dummyTempTubuh -= random(1, 5) / 10.0; // Turun 0.1 - 0.4 C
    } else if (random(0, 10) < 1) { // 10% kemungkinan suhu naik
      dummyTempTubuh += random(1, 3) / 10.0; // Naik 0.1 - 0.2 C
    }
    // Batasi suhu agar tidak terlalu ekstrem
    dummyTempTubuh = constrain(dummyTempTubuh, 28.0, 38.0);
    lastDummyTempChange = millis();
  }

  float tempLingkungan = random(200, 280) / 10.0; // 20.0 - 28.0 C
  float tempTubuh = dummyTempTubuh;

  // Simpan riwayat suhu untuk deteksi pemulihan
  if (millis() - lastTempSampleTime >= TEMP_SAMPLE_INTERVAL_MS) {
    tempHistory[tempHistoryIndex] = tempTubuh;
    tempHistoryIndex = (tempHistoryIndex + 1) % TEMP_HISTORY_SIZE;
    lastTempSampleTime = millis();
  }

  // 3. Cek tombol darurat
  bool buttonState = digitalRead(EMERGENCY_BTN);
  if (lastButtonState == HIGH && buttonState == LOW) {
    Serial.println("!!! TOMBOL DARURAT DITEKAN !!!");
    if (!emergencyActive) {
        emergencyActive = true;
        emergencyStart = millis();
        setHeaterPower(100, "emergency_start");
        heaterForcedOn = true; // Heater dihidupkan paksa
        digitalWrite(BUZZER_PIN, HIGH);
        buzzerActive = true;
        buzzerStartTime = millis();
        BTSerial.println("{\"event\":\"emergency_activated\"}");
    }
  }
  lastButtonState = buttonState;

  // 4. Reset status darurat setelah timeout
  if (emergencyActive && millis() - emergencyStart > EMERGENCY_TIMEOUT) {
    emergencyActive = false;
    if (!heaterForcedOn) { // Hanya matikan heater jika tidak ada severe hypo yang memaksa
      setHeaterPower(0, "emergency_end");
    }
    heaterForcedOn = false; // Reset forced on
    Serial.println("[TX] Emergency otomatis reset.");
    BTSerial.println("{\"event\":\"emergency_deactivated\"}");
  }

  // 5. Logika Mesin Status Hipotermia (hanya jika tidak dalam mode darurat)
  if (!emergencyActive) {
    if (tempTubuh > TEMP_NORMAL_THRESHOLD) {
      if (currentHypoState != NORMAL) {
        Serial.println("[HYPO] Status: NORMAL");
        currentHypoState = NORMAL;
        setHeaterPower(0, "normal_temp"); // Matikan heater jika suhu normal
        heaterForcedOn = false;
        BTSerial.println("{\"event\":\"hypothermia_status\",\"state\":\"normal\"}");
      }
    } else if (tempTubuh <= TEMP_MILD_HYPO_THRESHOLD) { // Severe Hypo
      if (currentHypoState != SEVERE_HYPO) {
        Serial.println("[HYPO] Status: SEVERE_HYPO");
        currentHypoState = SEVERE_HYPO;
        setHeaterPower(100, "severe_hypo"); // Heater 100%
        heaterForcedOn = true; // Heater dihidupkan paksa
        sendHypoNotification(tempTubuh); // Notifikasi dan buzzer
        BTSerial.println("{\"event\":\"hypothermia_status\",\"state\":\"severe\"}");
      }
      // Deteksi pemulihan dari Severe Hypo
      int countAboveMild = 0;
      int countBelowMild = 0;
      for (int i = 0; i < TEMP_HISTORY_SIZE; i++) {
        if (tempHistory[i] > TEMP_MILD_HYPO_THRESHOLD) {
          countAboveMild++;
        } else {
          countBelowMild++;
        }
      }
      if (currentHypoState == SEVERE_HYPO && countAboveMild > countBelowMild && (millis() - lastTempSampleTime < RECOVERY_WINDOW_MS)) { // Cek dalam jendela 20 detik
        Serial.println("[HYPO] Deteksi pemulihan. Memasuki mode STABILISASI.");
        currentHypoState = RECOVERING;
        stabilizationStartTime = millis();
        setHeaterPower(50, "recovering"); // Turunkan daya ke 50%
        heaterForcedOn = true; // Tetap paksa ON selama stabilisasi
        BTSerial.println("{\"event\":\"hypothermia_status\",\"state\":\"recovering\"}");
      }

    } else if (currentHypoState == RECOVERING) {
      // Cek jika suhu drop lagi (relaps)
      if (tempTubuh <= TEMP_MILD_HYPO_THRESHOLD) {
        Serial.println("[HYPO] Relaps! Kembali ke SEVERE_HYPO.");
        currentHypoState = SEVERE_HYPO;
        setHeaterPower(100, "severe_hypo_relapse");
        heaterForcedOn = true;
        sendHypoNotification(tempTubuh);
        BTSerial.println("{\"event\":\"hypothermia_status\",\"state\":\"severe\"}");
      } 
      // Cek jika waktu stabilisasi sudah selesai
      else if (millis() - stabilizationStartTime > STABILIZATION_DURATION_MS) {
        Serial.println("[HYPO] Stabilisasi selesai. Masuk ke MILD_HYPO_WARNING.");
        currentHypoState = MILD_HYPO_WARNING;
        setHeaterPower(0, "stabilization_end"); // Matikan pemanas, serahkan kontrol ke user
        heaterForcedOn = false;
        sendHypoNotification(tempTubuh); // Kirim notif mild warning
        BTSerial.println("{\"event\":\"hypothermia_status\",\"state\":\"mild_warning\"}");
      }
      // Jika tidak, tetap di mode RECOVERING dengan heater 50%

    } else if (tempTubuh > TEMP_MILD_HYPO_THRESHOLD && tempTubuh <= TEMP_NORMAL_THRESHOLD) { // Mild Hypo
      if (currentHypoState == NORMAL || currentHypoState == SEVERE_HYPO) {
        Serial.println("[HYPO] Status: MILD_HYPO_WARNING");
        currentHypoState = MILD_HYPO_WARNING;
        sendHypoNotification(tempTubuh); // Notifikasi dan buzzer
        BTSerial.println("{\"event\":\"hypothermia_status\",\"state\":\"mild_warning\"}");
      } else if (currentHypoState == MILD_HYPO_WARNING) {
        // Tetap di MILD_HYPO_WARNING, notifikasi akan diatur oleh cooldown
        sendHypoNotification(tempTubuh);
      } else if (currentHypoState == MILD_HYPO_IGNORED) {
        // Cek apakah countdown ignore sudah habis
        if (millis() - ignoreCountdownStartTime > IGNORE_DURATION_MS) {
          Serial.println("[HYPO] Countdown ignore habis. Kembali ke MILD_HYPO_WARNING.");
          currentHypoState = MILD_HYPO_WARNING;
          sendHypoNotification(tempTubuh); // Notifikasi dan buzzer
          BTSerial.println("{\"event\":\"hypothermia_status\",\"state\":\"mild_warning\"}");
        }
        // Jika masih dalam ignore, tidak melakukan apa-apa (buzzer/notif ditahan)
      }
    }
  }

  // 6. Kirim paket data LoRa secara periodik
  if (millis() - lastSend >= SEND_INTERVAL) {
    sendPacket(tempLingkungan, tempTubuh, emergencyActive);
    lastSend = millis();
  }

  // 7. Kirim ping LoRa
  if (millis() - lastPing >= PING_INTERVAL) {
    sendAckPing();
    lastPing = millis();
  }

  // 8. Handle LoRa incoming & update status
  handleLoRaIncoming();
  updateLED();
  updateBuzzer(); // Update buzzer state
}
