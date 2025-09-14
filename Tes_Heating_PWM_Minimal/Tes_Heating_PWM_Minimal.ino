#include <Arduino.h>

// Pin untuk mengontrol MOSFET/driver heating pad
#define HEATER_PIN 23
#define LED_BUILTIN 2 // LED untuk indikator status

// Variabel untuk menyimpan status power
int heaterPowerPercentage = 0;

// Fungsi untuk mengatur daya pemanas
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
}

void setup() {
  Serial.begin(115200);
  Serial.println("=== Uji Coba Pemanas PWM Minimalis ===");
  Serial.println("Masukkan persentase daya (0-100) via Serial Monitor.");

  // Konfigurasi pin LED
  pinMode(LED_BUILTIN, OUTPUT);

  // Konfigurasi pin pemanas sebagai output
  pinMode(HEATER_PIN, OUTPUT);
  
  // Matikan pemanas saat pertama kali dinyalakan
  setHeaterPower(0); 
}

void loop() {
  // Baca input dari Serial Monitor
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim(); // Hapus spasi di awal/akhir

    // Coba konversi input ke integer
    int power = command.toInt();
    
    // Atur daya pemanas
    setHeaterPower(power);
  }
  
  // Beri jeda singkat agar tidak membebani prosesor
  delay(20); 
}
