#ifndef NVS_DATA_H_INCLUDED
#define NVS_DATA_H_INCLUDED

void read_data_from_nvs(char* esp_wifi_sta_ssid, char* esp_wifi_sta_pass, light_info_t* light_info, char* mqtt_broker_uri);
void save_wifi_info_to_nvs(char* esp_wifi_sta_ssid, char* esp_wifi_sta_pass);
void save_light_info_to_nvs(light_info_t* light_info);
void save_mqtt_info_to_nvs(char* mqtt_broker_uri);

#endif