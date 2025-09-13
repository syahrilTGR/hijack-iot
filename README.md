# Hiker Tracking and Safety System based on LoRa

This project contains a collection of Arduino code (for the ESP32 platform) that builds a system to wirelessly monitor a hiker's location, temperature conditions, and emergency status using LoRa communication.

This repository is also related to the monitoring application, which can be found here: [https://github.com/syahrilTGR/ESP32_Monitor](https://github.com/syahrilTGR/ESP32_Monitor)

## System Architecture

The system consists of three main components:

1.  **Sender Node (TX):** The device carried by the hiker. It is responsible for reading sensor data and transmitting it periodically.
2.  **Repeater Node:** A device placed between the TX and RX to extend the range of the LoRa signal.
3.  **Receiver Node (RX):** The device at the monitoring post that receives, processes, and displays data from the hiker.

---

## Directory & Code Structure

Here is an explanation for each directory and the `.ino` file within it.

### Sender Node (Transmitter/TX)

The device carried by the hiker. There are several versions with different functionalities.

-   `TX/`
    *   **Initial Version.** Reads temperature sensors (DS18B20 & MLX90614) and GPS, then sends the data via LoRa.

-   `Tes1/`
    *   **Version with Actuators.** An enhancement of `TX/`, with the addition of a **Buzzer** and a **Heating Pad** controlled by on/off signals.

-   `tx2/`
    *   **Dummy LoRa Version.** Sends dummy JSON data via LoRa. Useful for testing the LoRa communication chain (TX -> Repeater -> RX) without needing physical sensors.

-   `tesHeating_withDummy/`
<<<<<<< HEAD
    *   **Full App Test Version.** A combination of `Tes1/` and `tx2/`. Sends **dummy** sensor data via LoRa, but has full functionality for **heater control via PWM (using `analogWrite`) through Bluetooth**. Ideal for comprehensive mobile app development.

-   `Tes_Heating_PWM/`
    *   **Heater Test Version.** Minimalist code containing only the **heater control function via PWM (using `analogWrite`) through Bluetooth**. Created specifically to test the UI component (slider) in the application in isolation.
=======
    *   **Full App Test Version.** A combination of `Tes1/` and `tx2/`. Sends **dummy** sensor data via LoRa, but has full functionality for **heater control via PWM through Bluetooth**. Ideal for comprehensive mobile app development.

-   `Tes_Heating_PWM/`
    *   **Heater Test Version.** Minimalist code containing only the **heater control function via PWM through Bluetooth**. Created specifically to test the UI component (slider) in the application in isolation.
>>>>>>> 41edc212ff4eaaff892bd63c0bbca4c4b1a472ad

### Receiver Node (Receiver/RX)

The device located at the monitoring post.

-   `RX/`
    *   **Basic Receiver.** Receives LoRa data and displays it on the Serial Monitor.

-   `tes2/`
    *   **Receiver via Bluetooth.** A variant of `RX/` that forwards the received data to an application (e.g., a smartphone) in JSON format via Bluetooth Serial.

-   `rx2/`
<<<<<<< HEAD
    *   **Main Receiver (Web Dashboard).** The most advanced version. Receives LoRa data, then serves it through a **WebSocket Server**. It now includes a **blinking LED indicator** for successful data reception and can receive data **directly from the Sender** (TX) in addition to receiving from the Repeater. The `BUZZER_PIN` has been moved to avoid conflict with the LED. It has a WiFi configuration portal if credentials are not yet saved.
=======
    *   **Main Receiver (Web Dashboard).** The most advanced version. Receives LoRa data, then serves it through a **WebSocket Server**. If connected to WiFi, the data can be accessed in real-time from a web dashboard. It has a WiFi configuration portal if credentials are not yet saved.
>>>>>>> 41edc212ff4eaaff892bd63c0bbca4c4b1a472ad

### Network Infrastructure

-   `repeater/`
<<<<<<< HEAD
    *   Acts as a bridge. Receives LoRa messages from the TX, then forwards them to the RX (and vice versa for ACK messages) to extend the range. The LoRa module's M0 and M1 pins are **hardwired to GND** for transparent mode operation.
=======
    *   Acts as a bridge. Receives LoRa messages from the TX, then forwards them to the RX (and vice versa for ACK messages) to extend the range.
>>>>>>> 41edc212ff4eaaff892bd63c0bbca4c4b1a472ad

### Testing & Prototyping Code

-   `GPS/`
    *   Simple code to verify the functionality of the GPS module.

-   `dummy/`
    *   Simulates a Receiver Node (RX) that sends dummy data via Bluetooth. Useful for the initial stages of mobile app development.

-   `dummy-wifi/`
    *   **Standalone Server (UI Dev).** Very useful code for frontend/dashboard development. The ESP32 will become a standalone server that provides dummy sensor data via WebSocket without needing a TX or Repeater node at all.

---

## LoRa Communication Flow

This project uses a simple addressing scheme to ensure data reaches the correct destination through the repeater.

1.  **TX (Address 1)** sends a message to the **Repeater (Address 2)**.
2.  **Repeater (Address 2)** receives the message, then forwards it to the **RX (Address 3)**.
3.  **RX (Address 3)** replies with an `ACK` to the **Repeater (Address 2)**.
4.  **Repeater (Address 2)** forwards the `ACK` to the **TX (Address 1)**.
<<<<<<< HEAD
5.  **New:** **RX (Address 3)** can also directly receive messages from **TX (Address 1)** if it is within range, providing a more robust communication link.
=======
>>>>>>> 41edc212ff4eaaff892bd63c0bbca4c4b1a472ad

---

## Development & Testing Guide

-   **For Mobile App Testing (Heater & Data Features):**
<<<<<<< HEAD
    *   Use `tesHeating_withDummy/tesHeating_withDummy.ino`. This provides heater control functionality (using `analogWrite`) and LoRa data transmission (dummy) simultaneously.
=======
    *   Use `tesHeating_withDummy/tesHeating_withDummy.ino`. This provides heater control functionality and LoRa data transmission (dummy) simultaneously.
>>>>>>> 41edc212ff4eaaff892bd63c0bbca4c4b1a472ad

-   **For Web Dashboard UI Testing:**
    *   Use `dummy-wifi/dummy-wifi.ino`. With just this one ESP32, you can focus on developing the frontend display without thinking about other hardware.

-   **For LoRa Communication Chain Testing:**
<<<<<<< HEAD
    *   Use `tx2/` (sender), `repeater/` (repeater), and `rx2/` (receiver) to validate the end-to-end data flow.
=======
    *   Use `tx2/` (sender), `repeater/` (repeater), and `rx2/` (receiver) to validate the end-to-end data flow.
>>>>>>> 41edc212ff4eaaff892bd63c0bbca4c4b1a472ad
