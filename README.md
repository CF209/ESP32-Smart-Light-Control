<img src="/images/home_page.png">

 This project was created for a custom ESP32 device I designed to run off 12V and use PWM outputs to control 12V lights. The project started as a very simple program with many things hard-coded and no standalone interface to a fully standalone device with a web interface, much more customizability, and auto configuration in Home Assistant. Above you can see the standard web interface.
 
 On first startup with no data saved, the device starts the Wifi in softAP mode with SSID "esp32_wifi_%s" where %s is a unique string derived from the device's MAC address, and password of simply "password". The device then starts a webserver that can be accessed at http://my-esp32.local/
 
 From the web page, you can connect the device to a wifi network by clicking the "Wifi Setup" option on the left-side menu, entering the network info, and clicking connect. The device will then try to connect to the wifi network with the information provided. If it succeeds, it then hosts the same webserver on the new network. If it fails, it defaults back to softAP mode, but re-attempts to connect every 60 seconds as long as no other devices are connected to the AP. The wifi data is also saved in NVS so on subsequent reboots it will automatically connect to the same network.

 The ESP32 device starts with 4 PWM light outputs configured as "Light 0", "Light 1", "Light 2", and "Light 3" on GPIO 7, 6, 5, and 4 respectively. These GPIO numbers are hard coded since the program was written for a specific device I designed, but can be changed in the /main/lights_ledc.c file. From the "Lights Setup" menu option on the left side, you can change the name of the lights and enable/disable them if you don't need all four. These settings are also saved in NVS and reloaded at startup. With the lights setup, you can control them from the home page in the web interface as seen above.
 
 To connect the device to Home Assistant, you must have an MQTT server setup. I have Mosquitto MQTT running on the same Raspberry Pi as Home Assistant. In the web interface, select the menu option for "MQTT Setup". Enter the URI for the MQTT broker. The MQTT status is shown on the left side menu along with the Wifi status, so you can see when it is connected. The MQTT broker URI is also saved to NVS so it can automatically connect on startup.
 
 Once the MQTT server is connected, configuration messages are automatically sent to Home Assistant to configure the lights. In Home Assistant you need to have the MQTT integration installed with discovery enabled. If all goes smoothly, the lights should automatically appear in Home Assistant with the same name as you set on the "Lights Setup" page!
 
 Lastly, you can update the firmware over the air by selecting the "Update FW" option from the menu. This link brings you to a different page that I borrowed from another project for OTA updates where you can upload a new binary FW file. The default username and password are both "admin" for this page.
 
<img src="/images/hass_lights.png" width="300">
