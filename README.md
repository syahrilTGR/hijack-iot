# Arduino Code Project: hijack-iot

A collection of Arduino codes for various purposes related to monitoring and data communication. This project is part of or related to [ESP32_Monitor on GitHub](https://github.com/syahrilTGR/ESP32_Monitor).

## Folder Descriptions

The following is an explanation for each sketch (.ino) in this repository:

### `/dummy`

*   `dummy.ino`: A sketch for a "dummy" receiver (RX) device. This code does not use a real LoRa module, but instead sends fake (dummy) JSON data via Bluetooth Serial. This sketch can also receive WiFi credential configuration (SSID & Password) via Bluetooth commands to then try to connect to a WiFi network.

### `/dummy-wifi`

*   `dummy-wifi.ino`: This sketch turns the ESP32 into a standalone device connected to WiFi.
    *   **Configuration Mode:** If no WiFi credentials are saved, the ESP32 will create an Access Point (AP) named "ESP32_Config". Users can connect to this AP and send WiFi credentials via HTTP POST.
    *   **Connected Mode:** After connecting to WiFi, the device will run a WebSocket server. This server periodically sends dummy sensor data (location, temperature, etc.) in JSON format to all connected clients.
    *   **Additional Features:** It has a physical emergency button and can receive user data updates (age, gender, etc.) from clients via WebSocket.

### `/GPS`

*   `GPS.ino`: A simple sketch to test a GPS module. This code reads data from a GPS module connected to `Serial2` (pins RX 16, TX 17) and displays it on the Serial Monitor, including latitude, longitude, speed, altitude, and time.

### `/RX`

*   `RX.ino`: The sketch for the main receiver (RX) device or "Guard Post". This code receives sensor data sent via a LoRa module, parses the data (coordinates, temperature, time, emergency status), and displays it on the Serial Monitor. This sketch also implements an acknowledgment (ACK) system to ensure data is received.

### `/TX`

*   `TX.ino`: The sketch for the transmitter (TX) device or "Hiker Node". This code is responsible for:
    1.  Reading data from various sensors:
        *   **DS18B20:** Ambient temperature.
        *   **MLX90614:** Non-contact body temperature.
        *   **GPS:** Location (latitude and longitude) and time.
    2.  Sending formatted data packets via a LoRa module periodically.
    3.  It has an emergency button to send a priority signal.

### `/Tes1`

*   `Tes1.ino`: A variant of the `TX/TX.ino` sketch. It has the same functionality with the addition of a **buzzer**. The buzzer will sound when the emergency button is pressed as an audio indicator.

### `/tes2`

*   `tes2.ino`: A variant of the `RX/RX.ino` sketch. In addition to receiving and parsing data from LoRa, this sketch also forwards the received data to another device (like a smartphone or laptop) in JSON format via **Bluetooth Serial**. This allows the data to be directly processed or displayed in other applications.