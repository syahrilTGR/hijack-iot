#include <BluetoothSerial.h>

// Pin untuk mengontrol MOSFET/driver heating pad
#define HEATER_PIN 23
#define LED_BUILTIN 2 // LED untuk indikator status

// Objek Bluetooth Serial
BluetoothSerial BTSerial;

// Variabel untuk menyimpan status power
int heaterPowerPercentage = 0;

// Fungsi untuk mengatur daya pemanas dan mengirim status kembali
void setHeaterPower(int percentage) {
  // Batasi nilai input antara 0 dan 100
  heaterPowerPercentage = constrain(percentage, 0, 100);
  
  // Konversi persentase (0-100) ke nilai PWM (0-255)
  int dutyCycle = map(heaterPowerPercentage, 0, 100, 0, 255);
  
  // Atur duty cycle PWM
  analogWrite(HEATER_PIN, dutyCycle);
  
  // Beri indikasi visual di LED Built-in
  if (dutyCycle > 0) {
    digitalWrite(LED_BUILTIN, HIGH);
  } else {
    digitalWrite(LED_BUILTIN, LOW);
  }
  
  // Cetak status ke Serial Monitor untuk debug
  Serial.printf("[HEATER] Power diatur ke %d%% (Duty Cycle: %d)\n", heaterPowerPercentage, dutyCycle);
  
  // Kirim status terbaru ke aplikasi dalam format JSON
  BTSerial.printf("{\"power\":%d}\n", heaterPowerPercentage);
}

// Fungsi untuk menangani perintah dari Bluetooth
void handleBluetooth() {
  if (BTSerial.available()) {
    String cmd = BTSerial.readStringUntil('\n');
    cmd.trim();
    Serial.printf("[BT] Perintah diterima: %s\n", cmd.c_str());

    // Perintah untuk mengatur PWM, format: "PWM:80"
    if (cmd.startsWith("PWM:")) {
      int power = cmd.substring(4).toInt();
      setHeaterPower(power);
    } 
    // Perintah untuk mendapatkan status terakhir
    else if (cmd == "GET_STATUS") {
      // Kirim kembali status power saat ini
      BTSerial.printf("{\"power\":%d}\n", heaterPowerPercentage);
    }
    else {
      // Balasan jika perintah tidak dikenali
      BTSerial.println("{\"error\":\"unknown_command\"}");
    }
  }
}

void setup() {
  Serial.begin(115200);
  
  // Mulai Bluetooth Serial dengan nama yang mudah dikenali
  BTSerial.begin("ESP32-Heater-Test");
  Serial.println("=== Uji Coba Pemanas PWM via Bluetooth ===");
  Serial.println("Perangkat Bluetooth siap untuk di-pairing.");

  // Konfigurasi pin LED
  pinMode(LED_BUILTIN, OUTPUT);

  // Konfigurasi pin pemanas sebagai output
  pinMode(HEATER_PIN, OUTPUT);
  
  // Matikan pemanas saat pertama kali dinyalakan
  setHeaterPower(0); 
}

void loop() {
  // Cukup panggil fungsi untuk handle Bluetooth
  handleBluetooth();
  
  // Beri jeda singkat agar tidak membebani prosesor
  delay(20); 
}
