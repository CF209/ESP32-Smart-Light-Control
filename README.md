This project was created as a base for creating custum ESP32 sensors and controllers using the ESP IDF instead of the Arduino IDE.

On startup, the ESP32 attempts to connect to wifi if there is a saved wifi network. If not, it starts in soft AP mode.

It then hosts a web server that allows for multiple different functions through the web interface:
 - Control an LED
 - Update the Wifi info
   - Once updated, the ESP32 will attempt to connect to the new network
   - The wifi info is also saved to NVS so it can reconnect after a reboot
   - If it can't connect to the new network, it defaults back to soft AP mode
   - In soft AP mode if no devices are connected to the AP, it periodically retries to connect to the wifi network
 - Over the air updates

I eventually want to also use the web interface for live monitoring of sensors, and to connect the ESP32 to an MQTT server to send/receive data from home assistant.
