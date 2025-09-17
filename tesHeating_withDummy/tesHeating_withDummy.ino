#include "BluetoothSerial.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// =================================
//      PIN & KONFIGURASI
// =================================
#define BUZZER_PIN 2
#define HEATER_PIN 23

// =================================
//      OBJEK & STATUS
// =================================

BluetoothSerial BTSerial;
TaskHandle_t buzzerTaskHandle = NULL;

// Enum untuk status hipotermia
enum HypoStatus { NORMAL, MILD, SEVERE };
HypoStatus currentStatus = NORMAL;

// Variabel data sensor
float bodyTemp = 37.0;
float ambientTemp = 25.0;

// Variabel data GPS (dummy)
float gpsLat = -6.9175; // Contoh: Bandung
float gpsLon = 107.6191;

// Status emergency & pemanas
bool emergencyTriggered = false;
int heaterPowerPercentage = 0;
int lastHeaterPowerPercentage = 50; // Menyimpan setelan daya terakhir, default 50%
bool isHeaterOn = false; // Status ON/OFF pemanas
uint32_t lastMildIgnoreTime = 0;
bool mildIgnored = false;

// Pengaturan waktu
uint32_t lastDataSendTime = 0;
const uint32_t DATA_SEND_INTERVAL_MS = 2000; // Kirim data setiap 2 detik

// Variabel untuk deteksi pemulihan
const int RECOVERY_WINDOW_SECONDS = 20;
float tempHistory[RECOVERY_WINDOW_SECONDS];
int historyIndex = 0;
uint32_t lastHistoryUpdateTime = 0;

// =================================
//      FUNGSI-FUNGSI BUZZER
// =================================

void buzzerLoop(void *parameter) {
  int pattern = *((int*)parameter);
  while (1) {
    if (pattern == 1) { // Pola MILD: Beep setiap 2 detik
      digitalWrite(BUZZER_PIN, HIGH);
      vTaskDelay(200 / portTICK_PERIOD_MS);
      digitalWrite(BUZZER_PIN, LOW);
      vTaskDelay(1800 / portTICK_PERIOD_MS);
    } else if (pattern == 2) { // Pola SEVERE/EMERGENCY: Beep cepat
      digitalWrite(BUZZER_PIN, HIGH);
      vTaskDelay(100 / portTICK_PERIOD_MS);
      digitalWrite(BUZZER_PIN, LOW);
      vTaskDelay(100 / portTICK_PERIOD_MS);
    } else { // Pola tidak diketahui, matikan
      digitalWrite(BUZZER_PIN, LOW);
      vTaskDelete(buzzerTaskHandle);
    }
  }
}

void startBuzzer(int pattern) {
  if (buzzerTaskHandle != NULL) {
    vTaskDelete(buzzerTaskHandle);
  }
  static int task_pattern;
  task_pattern = pattern;
  xTaskCreate(
    buzzerLoop,
    "BuzzerTask",
    1000,
    &task_pattern,
    1,
    &buzzerTaskHandle
  );
  Serial.printf("[BUZZER] Pola %d diaktifkan.\n", pattern);
}

void stopBuzzer() {
  if (buzzerTaskHandle != NULL) {
    vTaskDelete(buzzerTaskHandle);
    buzzerTaskHandle = NULL;
  }
  digitalWrite(BUZZER_PIN, LOW);
  Serial.println("[BUZZER] Buzzer dimatikan.");
}


// =================================
//      FUNGSI SETUP
// =================================
void setup() {
  Serial.begin(115200);
  BTSerial.begin("JaketPemanas_ESP32");

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(HEATER_PIN, OUTPUT);

  setHeaterPower(0);

  for (int i = 0; i < RECOVERY_WINDOW_SECONDS; i++) {
    tempHistory[i] = 37.0;
  }

  Serial.println("\n===============================================");
  Serial.println("Sistem Kontrol Jaket Pemanas Siap.");
  Serial.println("-----------------------------------------------");
  Serial.println("-> Input via Serial Monitor:");
  Serial.println("   - Suhu Tubuh: T<nilai>");
  Serial.println("   - Suhu Lingkungan: L<nilai>");
  Serial.println("   - Kontrol: HEAT_ON, HEAT_OFF");
  Serial.println("-----------------------------------------------");
  Serial.println("-> Perangkat Bluetooth siap: 'JaketPemanas_ESP32'");
  Serial.println("   Input via BT: P<0-100>, HEAT_ON, HEAT_OFF, E (emergency), IGNORE_MILD, ACTIVATE_HEATER");
  Serial.println("===============================================");
}

// =================================
//      FUNGSI LOOP UTAMA
// =================================
void loop() {
  handleAppInput();
  handleSerialMonitorInput();
  checkHypothermiaStatus();

  if (millis() - lastDataSendTime >= DATA_SEND_INTERVAL_MS) {
    sendDataToApp();
    lastDataSendTime = millis();
  }

  if (millis() - lastHistoryUpdateTime >= 1000) {
    tempHistory[historyIndex] = bodyTemp;
    historyIndex = (historyIndex + 1) % RECOVERY_WINDOW_SECONDS;
    lastHistoryUpdateTime = millis();
  }
  
  delay(100);
}

// =================================
//      FUNGSI-FUNGSI PEMBANTU
// =================================

void setHeaterPower(int powerPercent) {
  powerPercent = constrain(powerPercent, 0, 100);
  heaterPowerPercentage = powerPercent;

  if (powerPercent > 0) {
    isHeaterOn = true;
    lastHeaterPowerPercentage = powerPercent;
  } else {
    isHeaterOn = false;
  }

  int dutyCycle = map(heaterPowerPercentage, 0, 100, 0, 255);
  analogWrite(HEATER_PIN, dutyCycle);
  Serial.printf("[INFO] Pemanas diatur ke %d%%. Status: %s. (Setting terakhir: %d%%)\n", heaterPowerPercentage, isHeaterOn ? "ON" : "OFF", lastHeaterPowerPercentage);
}

void handleAppInput() {
  if (BTSerial.available() > 0) {
    String input = BTSerial.readStringUntil('\n');
    input.trim();
    input.toUpperCase();
    Serial.printf("[BT_IN] Diterima: %s\n", input.c_str());

    if (input.startsWith("E")) {
      emergencyTriggered = true;
      Serial.println("[ALERT] Tombol darurat diaktifkan via App!");
      startBuzzer(2); // Pola buzzer cepat
      sendDataToApp();
      emergencyTriggered = false;
      return;
    }

    if (input == "IGNORE_MILD") {
        if (currentStatus == MILD) {
            stopBuzzer();
            mildIgnored = true;
            lastMildIgnoreTime = millis();
            String jsonResponse = "{\"status\":\"OK\",\"message\":\"Peringatan MILD diabaikan.\"}";
            BTSerial.println(jsonResponse);
        } else {
            String jsonResponse = "{\"status\":\"ERROR\",\"message\":\"Tidak ada peringatan MILD yang aktif.\"}";
            BTSerial.println(jsonResponse);
        }
        return;
    }

    if (input == "ACTIVATE_HEATER") {
        if (currentStatus == MILD) {
            setHeaterPower(lastHeaterPowerPercentage);
            stopBuzzer();
            mildIgnored = false; // User took action, so reset ignore
            String jsonResponse = "{\"status\":\"OK\",\"message\":\"Pemanas diaktifkan sesuai permintaan.\"}";
            BTSerial.println(jsonResponse);
        } else {
            String jsonResponse = "{\"status\":\"ERROR\",\"message\":\"Perintah hanya berlaku pada status MILD.\"}";
            BTSerial.println(jsonResponse);
        }
        return;
    }

    if (input == "HEAT_ON") {
      if (currentStatus != SEVERE) {
        setHeaterPower(lastHeaterPowerPercentage);
        String jsonResponse = "{\"status\":\"OK\",\"message\":\"Heater ON to " + String(lastHeaterPowerPercentage) + "%%\"}";
        BTSerial.println(jsonResponse);
      } else {
        Serial.println("[WARN] Perintah HEAT_ON ditolak. Status SEVERE aktif.");
        String jsonResponse = "{\"status\":\"ERROR\",\"message\":\"Command denied in SEVERE state.\"}";
        BTSerial.println(jsonResponse);
      }
      return;
    }

    if (input == "HEAT_OFF") {
      if (currentStatus != SEVERE) {
        setHeaterPower(0);
        String jsonResponse = "{\"status\":\"OK\",\"message\":\"Heater OFF\"}";
        BTSerial.println(jsonResponse);
      } else {
        Serial.println("[WARN] Perintah HEAT_OFF ditolak. Status SEVERE aktif.");
        String jsonResponse = "{\"status\":\"ERROR\",\"message\":\"Command denied in SEVERE state.\"}";
        BTSerial.println(jsonResponse);
      }
      return;
    }
    
    if (input.startsWith("P")) {
      int powerValue = input.substring(1).toInt();
      if (currentStatus != SEVERE) {
        setHeaterPower(powerValue);
        String jsonResponse = "{\"status\":\"OK\",\"message\":\"Heater power set to " + String(powerValue) + "%%\"}";
        BTSerial.println(jsonResponse);
      } else {
        Serial.println("[WARN] Pengaturan manual ditolak. Status SEVERE aktif.");
        String jsonResponse = "{\"status\":\"ERROR\",\"message\":\"Manual control denied in SEVERE state.\"}";
        BTSerial.println(jsonResponse);
      }
      return;
    }
  }
}

void handleSerialMonitorInput() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    input.toUpperCase();
    Serial.printf("[SERIAL_IN] Diterima: %s\n", input.c_str());

    if (input == "HEAT_ON") {
      if (currentStatus != SEVERE) {
        setHeaterPower(lastHeaterPowerPercentage);
      } else {
        Serial.println("[WARN] Perintah HEAT_ON ditolak. Status SEVERE aktif.");
      }
      return;
    }

    if (input == "HEAT_OFF") {
      if (currentStatus != SEVERE) {
        setHeaterPower(0);
      } else {
        Serial.println("[WARN] Perintah HEAT_OFF ditolak. Status SEVERE aktif.");
      }
      return;
    }

    if (input.startsWith("T")) {
      bodyTemp = input.substring(1).toFloat();
      Serial.printf("[SERIAL_IN] Suhu Tubuh diperbarui: %.2f C\n", bodyTemp);
    } else if (input.startsWith("L")) {
      ambientTemp = input.substring(1).toFloat();
      Serial.printf("[SERIAL_IN] Suhu Lingkungan diperbarui: %.2f C\n", ambientTemp);
    }
  }
}

void checkHypothermiaStatus() {
  HypoStatus lastStatus = currentStatus;

  if (bodyTemp < 32.0) {
    currentStatus = SEVERE;
  } else if (bodyTemp < 36.0) {
    if (currentStatus != SEVERE) {
       currentStatus = MILD;
    }
  } else {
    currentStatus = NORMAL;
  }

  if (currentStatus != lastStatus) {
    Serial.printf("[STATUS] Perubahan: %s -> %s\n", statusToString(lastStatus), statusToString(currentStatus));
    switch (currentStatus) {
      case NORMAL:
        stopBuzzer();
        setHeaterPower(0);
        mildIgnored = false; // Reset ignore status on recovery
        break;
      case MILD:
        if (!mildIgnored) { // Only notify if not currently ignored
          startBuzzer(1); // Pola buzzer lambat
          String jsonResponse = "{\"status\":\"WARNING\",\"warning_type\":\"MILD\",\"message\":\"Suhu tubuh rendah! Aktifkan pemanas?\"}";
          BTSerial.println(jsonResponse);
        }
        break;
      case SEVERE:
        startBuzzer(2); // Pola buzzer cepat
        setHeaterPower(100);
        mildIgnored = false; // Severe state overrides mild ignore
        {
          String jsonResponse = "{\"status\":\"ALERT\",\"alert_type\":\"SEVERE\",\"message\":\"Kondisi kritis! Pemanas diaktifkan secara otomatis.\"}";
          BTSerial.println(jsonResponse);
        }
        break;
    }
    sendDataToApp();
  } else { // currentStatus == lastStatus
    if (currentStatus == MILD && mildIgnored) {
      // Check for re-notification after 15 minutes
      const uint32_t MILD_REMINDER_INTERVAL_MS = 6 * 1000; // 15 minutes, tpi utk tes 6 detik saja
      if (millis() - lastMildIgnoreTime >= MILD_REMINDER_INTERVAL_MS) {
        Serial.println("[REMINDER] MILD condition persists after 15 minutes. Re-notifying.");
        mildIgnored = false; // Reset ignore status to allow re-notification
        startBuzzer(1); // Re-activate MILD buzzer pattern
        String jsonResponse = "{\"status\":\"WARNING\",\"warning_type\":\"MILD_REMINDER\",\"message\":\"Suhu tubuh masih rendah setelah 15 menit. Aktifkan pemanas?\"}";
        BTSerial.println(jsonResponse); // Send updated data to app
      }
    } else if (currentStatus == SEVERE) {
      checkRecovery();
    }
  }
}

void checkRecovery() {
  int aboveCount = 0;
  for (int i = 0; i < RECOVERY_WINDOW_SECONDS; i++) {
    if (tempHistory[i] > 32.0) {
      aboveCount++;
    }
  }

  if (aboveCount > RECOVERY_WINDOW_SECONDS / 2) {
    Serial.println("[RECOVERY] Kondisi membaik. Kembali ke status MILD.");
    currentStatus = MILD;
    setHeaterPower(0);
    startBuzzer(1); // Kembali ke peringatan MILD
    {
      String jsonResponse = "{\"status\":\"WARNING\",\"warning_type\":\"MILD\",\"message\":\"Kondisi membaik, pemanas dinonaktifkan. Suhu masih rendah.\"}";
      BTSerial.println(jsonResponse);
    }
  }
}

const char* statusToString(HypoStatus status) {
  switch (status) {
    case NORMAL: return "NORMAL";
    case MILD:   return "MILD";
    case SEVERE: return "SEVERE";
    default:     return "UNKNOWN";
  }
}

void sendDataToApp() {
  String json = "{\n";
  json += "  \"bodyTemp\": " + String(bodyTemp, 2) + ",\n";
  json += "  \"envTemp\": " + String(ambientTemp, 2) + ",\n";
  json += "  \"heaterPower\": " + String(heaterPowerPercentage) + ",\n";
  json += "  \"isHeaterOn\": " + String(isHeaterOn ? "true" : "false") + ",\n";
  json += "  \"gps\": { \"lat\": " + String(gpsLat, 4) + ", \"lon\": " + String(gpsLon, 4) + " },\n";
  json += String("  \"hypoStatus\": \"") + statusToString(currentStatus) + "\",\n";
  json += "  \"emergency\": " + String(emergencyTriggered ? "true" : "false") + "\n";
  json += "}";

  Serial.println("-- Mengirim data ke App (via BT) --");
  Serial.println(json);
  BTSerial.println(json);
}
