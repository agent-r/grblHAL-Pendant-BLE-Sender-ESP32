## grblHAL-Pendant-BLE-Sender (ESP32)

* This firmware runs on an ESP32 and acts as a wireless bridge between grblHAL and a BLE pendant.
* It connects to grblHAL, translates incoming JSON status messages, and exposes them as BLE characteristics.
* Likewise, commands from the pendant (jogging, G-code, messages) are received via BLE and forwarded to grblHAL.

## grblHAL ⇄ Serial2 ⇄ ESP32 ⇄ BLE ⇄ Pendant

* Repository of the grblHAL plugin: https://github.com/agent-r/grblHAL-Pendant-Plugin.git
* Repository of the Pendant: https://github.com/agent-r/grblHAL-Pendant.git

## how to set up the ESP32:

* Flash this code to your ESP32
* Connect your ESP32 to your grblHAL-Controller (in my case a Teensy4.1 / Pin 24/25)