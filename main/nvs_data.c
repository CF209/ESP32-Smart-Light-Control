#include <esp_log.h>
#include <esp_err.h>
#include <nvs_flash.h>

// Namespace for storing data
#define ESP_NVS_NAMESPACE "esp_saved_data"

// Keys for storing wifi data in NVS
#define ESP_NVS_SSID_KEY  "wifi_ssid"
#define WIFI_SSID_LENGTH  33
#define ESP_NVS_PASS_KEY  "wifi_pass"
#define WIFI_PASS_LENGTH  64

// Keys for storing light data in NVS
#define LIGHT_NAME_LENGTH 13
static const char* light_name_keys[] = {
    "l0_name",
    "l1_name",
    "l2_name",
    "l3_name"
};
static const char* light_enabled_keys[] = {
    "l0_en",
    "l1_en",
    "l2_en",
    "l3_en"
};

// Keys for storing MQTT data
#define ESP_NVS_MQTT_BROKER_KEY  "mqtt_uri"
#define MQTT_BROKER_LENGTH       257

typedef struct
{
  char name[13];
  uint8_t enabled;
  int duty_cycle;
} light_info_t;

// Debug tag for log statements
static const char *TAG = "NVS Data Storage";

// Reads saved NVS data on startup
void read_data_from_nvs(char* esp_wifi_sta_ssid, char* esp_wifi_sta_pass, light_info_t* light_info, char* mqtt_broker_uri)
{
    ESP_LOGI(TAG, "Opening Non-Volatile Storage (NVS) handle... ");
    nvs_handle_t esp_nvs_handle;
    esp_err_t err = nvs_open(ESP_NVS_NAMESPACE, NVS_READONLY, &esp_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Reading SSID from NVS ... ");
        size_t required_length = WIFI_SSID_LENGTH;
        err = nvs_get_str(esp_nvs_handle, ESP_NVS_SSID_KEY, esp_wifi_sta_ssid, &required_length);
        switch (err) {
            case ESP_OK:
                ESP_LOGI(TAG, "SSID = %s\n", esp_wifi_sta_ssid);
                break;
            case ESP_ERR_NVS_NOT_FOUND:
                ESP_LOGI(TAG, "The SSID value is not initialized yet!\n");
                break;
            default :
                ESP_LOGI(TAG, "Error (%s) reading!\n", esp_err_to_name(err));
        }
        ESP_LOGI(TAG, "Reading password from NVS ... ");
        required_length = WIFI_PASS_LENGTH;
        err = nvs_get_str(esp_nvs_handle, ESP_NVS_PASS_KEY, esp_wifi_sta_pass, &required_length);
        switch (err) {
            case ESP_OK:
                ESP_LOGI(TAG, "Password = %s\n", esp_wifi_sta_pass);
                break;
            case ESP_ERR_NVS_NOT_FOUND:
                ESP_LOGI(TAG, "The SSID value is not initialized yet!\n");
                break;
            default :
                ESP_LOGI(TAG, "Error (%s) reading!\n", esp_err_to_name(err));
        }
        for (int i = 0; i < 4; i++) {
            ESP_LOGI(TAG, "Reading light%d name from NVS ... ", i);
            required_length = LIGHT_NAME_LENGTH;
            err = nvs_get_str(esp_nvs_handle, light_name_keys[i], light_info[i].name, &required_length);
            switch (err) {
                case ESP_OK:
                    ESP_LOGI(TAG, "Light%d name = %s\n", i, light_info[i].name);
                    break;
                case ESP_ERR_NVS_NOT_FOUND:
                    ESP_LOGI(TAG, "The Light%d name is not initialized yet!\n", i);
                    break;
                default :
                    ESP_LOGI(TAG, "Error (%s) reading!\n", esp_err_to_name(err));
            }
            ESP_LOGI(TAG, "Reading light%d enabled from NVS ... ", i);
            err = nvs_get_u8(esp_nvs_handle, light_enabled_keys[i], &light_info[i].enabled);
            switch (err) {
                case ESP_OK:
                    ESP_LOGI(TAG, "Light%d enabled = %d\n", i, light_info[i].enabled);
                    break;
                case ESP_ERR_NVS_NOT_FOUND:
                    ESP_LOGI(TAG, "The Light%d enabled is not initialized yet!\n", i);
                    break;
                default :
                    ESP_LOGI(TAG, "Error (%s) reading!\n", esp_err_to_name(err));
            }
        }
        ESP_LOGI(TAG, "Reading MQTT Broker from NVS ... ");
        required_length = MQTT_BROKER_LENGTH;
        err = nvs_get_str(esp_nvs_handle, ESP_NVS_MQTT_BROKER_KEY, mqtt_broker_uri, &required_length);
        switch (err) {
            case ESP_OK:
                ESP_LOGI(TAG, "MQTT Broker = %s\n", mqtt_broker_uri);
                break;
            case ESP_ERR_NVS_NOT_FOUND:
                ESP_LOGI(TAG, "The MQTT Broker value is not initialized yet!\n");
                break;
            default :
                ESP_LOGI(TAG, "Error (%s) reading!\n", esp_err_to_name(err));
        }
        nvs_close(esp_nvs_handle);
    }
}

// Saves wifi data to NVS so it is preserved on reboot
void save_wifi_info_to_nvs(char* esp_wifi_sta_ssid, char* esp_wifi_sta_pass)
{
    ESP_LOGI(TAG, "Opening Non-Volatile Storage (NVS) handle... ");
    nvs_handle_t esp_nvs_handle;
    esp_err_t err = nvs_open(ESP_NVS_NAMESPACE, NVS_READWRITE, &esp_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    } else {
        // Save SSID
        ESP_LOGI(TAG, "Saving SSID to NVS ... ");
        err = nvs_set_str(esp_nvs_handle, ESP_NVS_SSID_KEY, esp_wifi_sta_ssid);
        switch (err) {
            case ESP_OK:
                ESP_LOGI(TAG, "SSID saved!");
                break;
            default :
                ESP_LOGI(TAG, "Error (%s) writing!\n", esp_err_to_name(err));
        }
        // Save password
        ESP_LOGI(TAG, "Saving password to NVS ... ");
        err = nvs_set_str(esp_nvs_handle, ESP_NVS_PASS_KEY, esp_wifi_sta_pass);
        switch (err) {
            case ESP_OK:
                ESP_LOGI(TAG, "Password saved!");
                break;
            default :
                ESP_LOGI(TAG, "Error (%s) wiriting!\n", esp_err_to_name(err));
        }
        ESP_LOGI(TAG, "Committing updates in NVS ... ");
        err = nvs_commit(esp_nvs_handle);
        switch (err) {
            case ESP_OK:
                ESP_LOGI(TAG, "Done");
                break;
            default :
                ESP_LOGI(TAG, "Error (%s)\n", esp_err_to_name(err));
        }
        nvs_close(esp_nvs_handle);
    }
}

// Saves light data to NVS so it is preserved on reboot
void save_light_info_to_nvs(light_info_t* light_info)
{
    ESP_LOGI(TAG, "Opening Non-Volatile Storage (NVS) handle... ");
    nvs_handle_t esp_nvs_handle;
    esp_err_t err = nvs_open(ESP_NVS_NAMESPACE, NVS_READWRITE, &esp_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    } else {
        for (int i = 0; i < 4; i++) {
            // Save light name
            ESP_LOGI(TAG, "Saving light%d name to NVS ... ", i);
            err = nvs_set_str(esp_nvs_handle, light_name_keys[i], light_info[i].name);
            switch (err) {
                case ESP_OK:
                    ESP_LOGI(TAG, "Light%d saved!", i);
                    break;
                default :
                    ESP_LOGI(TAG, "Error (%s) writing!\n", esp_err_to_name(err));
            }
            // Save light enabled status
            ESP_LOGI(TAG, "Saving light%d enabled status to NVS ... ", i);
            err = nvs_set_u8(esp_nvs_handle, light_enabled_keys[i], light_info[i].enabled);
            switch (err) {
                case ESP_OK:
                    ESP_LOGI(TAG, "Light%d enabled status saved!", i);
                    break;
                default :
                    ESP_LOGI(TAG, "Error (%s) wiriting!\n", esp_err_to_name(err));
            }
        }
        ESP_LOGI(TAG, "Committing updates in NVS ... ");
        err = nvs_commit(esp_nvs_handle);
        switch (err) {
            case ESP_OK:
                ESP_LOGI(TAG, "Done");
                break;
            default :
                ESP_LOGI(TAG, "Error (%s)\n", esp_err_to_name(err));
        }
        nvs_close(esp_nvs_handle);
    }
}

// Saves MQTT data to NVS so it is preserved on reboot
void save_mqtt_info_to_nvs(char* mqtt_broker_uri)
{
    ESP_LOGI(TAG, "Opening Non-Volatile Storage (NVS) handle... ");
    nvs_handle_t esp_nvs_handle;
    esp_err_t err = nvs_open(ESP_NVS_NAMESPACE, NVS_READWRITE, &esp_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    } else {
        // Save MQTT Broker
        ESP_LOGI(TAG, "Saving MQTT Broker to NVS ... ");
        err = nvs_set_str(esp_nvs_handle, ESP_NVS_MQTT_BROKER_KEY, mqtt_broker_uri);
        switch (err) {
            case ESP_OK:
                ESP_LOGI(TAG, "MQTT Broker saved!");
                break;
            default :
                ESP_LOGI(TAG, "Error (%s) writing!\n", esp_err_to_name(err));
        }
        ESP_LOGI(TAG, "Committing updates in NVS ... ");
        err = nvs_commit(esp_nvs_handle);
        switch (err) {
            case ESP_OK:
                ESP_LOGI(TAG, "Done");
                break;
            default :
                ESP_LOGI(TAG, "Error (%s)\n", esp_err_to_name(err));
        }
        nvs_close(esp_nvs_handle);
    }
}