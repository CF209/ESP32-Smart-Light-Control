/*
WIFI IDF Test
This project was created to test out some basic functions on the ESP32 including:
  - Wifi station and AP mode and switching between the two
  - OTA updates
  - NVS storing data
  - LED PWM outputs
  - HTTP servers

TO DO:
 - Update webpage so it works without access to internet
 - Update OTA page to fit into new website format
 - Investigate these errors that pop up when starting the webserver (it still seems to work fine though)
    httpd: httpd_server_init: error in listen (112)
*/


#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include <nvs_flash.h>
#include <esp_netif.h>
#include <esp_eth.h>
#include <esp_ota_ops.h>
#include <esp_flash_partitions.h>
#include <esp_partition.h>
#include <esp_tls_crypto.h>
#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_err.h>
#include <mdns.h>

#include "jsmn.h"

// Moved the lights functions to a separate file to clean up code
#include "lights_ledc.h"

// Specify the max time and max number of retries for connecting
// to wifi before switching to AP mode, whichever comes first
#define ESP_MAXIMUM_CONNECT_RETRY  10
#define ESP_WIFI_CONNECT_WAIT      15000

// Wifi data for AP mode. The program adds the ESP MAC address to the end of the SSID to avoid conflicts
#define ESP_WIFI_AP_SSID           "esp_wifi"
#define ESP_WIFI_AP_PASS           "password"
#define ESP_WIFI_AP_CHANNEL        1
#define ESP_WIFI_AP_MAX_STA_CONN   4

// Hostname for mDNS service. "my-esp32" becomes http://my-esp32.local/
#define ESP_HOSTNAME    "my-esp32"

// Keys for storing wifi data in NVS so it is preserved on reboot
#define ESP_NVS_NAMESPACE "wifi_data"
#define ESP_NVS_SSID_KEY  "wifi_ssid"
#define ESP_NVS_PASS_KEY  "wifi_pass"

// Debug tag for log statements
static const char *TAG = "wifi idf test";

// Tracks the number of retries for connecting to Wifi
static int wifi_retry_count = 0;

// Saves the current state of the lights
typedef struct
{
  char name[13];
  uint8_t enabled;
  int duty_cycle;
} light_info_t;

static light_info_t light_data[4];

// Flags to track if wifi is connected and to trigger a reconnect if new data is entered
static uint8_t wifi_connected = 0;
static uint8_t new_wifi_info = 0;
static uint8_t ap_mode = 0;

// Length of wifi data char arrays
#define WIFI_SSID_LENGTH 33
#define WIFI_PASS_LENGTH 64

// Char arrays for storing wifi data
// If initialized here, NVS data will be ignored
// To initialize from NVS leave as ""
static char esp_wifi_sta_ssid[WIFI_SSID_LENGTH] = "";
static char esp_wifi_sta_pass[WIFI_PASS_LENGTH] = "";

static char mqtt_broker_uri[257] = "";

// Struct to store authorization details for OTA
typedef struct
{
  const char *username;
  const char *password;
} basic_auth_info_t;

// Username and password for OTA updates
static basic_auth_info_t auth_info = 
{
  .username = "admin",
  .password = "admin",
};

#define HTTPD_401      "401 UNAUTHORIZED"           /*!< HTTP Response 401 */

char auth_buffer[512];

//Read HTML files into char arrays
extern const char html_ota[] asm("_binary_ota_html_start");
extern const char html_index[] asm("_binary_index_html_start");


// Reads saved wifi info from NVS and stores it in global variables
// esp_wifi_sta_ssid and esp_wifi_sta_pass
static void read_wifi_info_from_nvs()
{
    ESP_LOGI(TAG, "Opening Non-Volatile Storage (NVS) handle... ");
    nvs_handle_t wifi_nvs_handle;
    esp_err_t err = nvs_open(ESP_NVS_NAMESPACE, NVS_READONLY, &wifi_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Reading SSID from NVS ... ");
        size_t required_length = sizeof(esp_wifi_sta_ssid);
        err = nvs_get_str(wifi_nvs_handle, ESP_NVS_SSID_KEY, esp_wifi_sta_ssid, &required_length);
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
        required_length = sizeof(esp_wifi_sta_pass);
        err = nvs_get_str(wifi_nvs_handle, ESP_NVS_PASS_KEY, esp_wifi_sta_pass, &required_length);
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
        nvs_close(wifi_nvs_handle);
    }
}

// Saves wifi data stored in global variables
// esp_wifi_sta_ssid and esp_wifi_sta_pass to NVS
static void save_wifi_info_to_nvs()
{
    ESP_LOGI(TAG, "Opening Non-Volatile Storage (NVS) handle... ");
    nvs_handle_t wifi_nvs_handle;
    esp_err_t err = nvs_open(ESP_NVS_NAMESPACE, NVS_READWRITE, &wifi_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    } else {
        // Save SSID
        ESP_LOGI(TAG, "Saving SSID to NVS ... ");
        err = nvs_set_str(wifi_nvs_handle, ESP_NVS_SSID_KEY, esp_wifi_sta_ssid);
        switch (err) {
            case ESP_OK:
                ESP_LOGI(TAG, "SSID saved!");
                break;
            default :
                ESP_LOGI(TAG, "Error (%s) writing!\n", esp_err_to_name(err));
        }
        // Save password
        ESP_LOGI(TAG, "Saving password to NVS ... ");
        err = nvs_set_str(wifi_nvs_handle, ESP_NVS_PASS_KEY, esp_wifi_sta_pass);
        switch (err) {
            case ESP_OK:
                ESP_LOGI(TAG, "Password saved!");
                break;
            default :
                ESP_LOGI(TAG, "Error (%s) wiriting!\n", esp_err_to_name(err));
        }
        ESP_LOGI(TAG, "Committing updates in NVS ... ");
        err = nvs_commit(wifi_nvs_handle);
        switch (err) {
            case ESP_OK:
                ESP_LOGI(TAG, "Done");
                break;
            default :
                ESP_LOGI(TAG, "Error (%s)\n", esp_err_to_name(err));
        }
        nvs_close(wifi_nvs_handle);
    }
}

// Borrowed the HTTP authorization and OTA code in the
// next few functions from another project

//-----------------------------------------------------------------------------
static char *http_auth_basic( const char *username, const char *password )
{
  int out;
  char user_info[128];
  static char digest[512];
  size_t n = 0;
  sprintf( user_info, "%s:%s", username, password );

  esp_crypto_base64_encode( NULL, 0, &n, ( const unsigned char * )user_info, strlen( user_info ) );

  // 6: The length of the "Basic " string
  // n: Number of bytes for a base64 encode format
  // 1: Number of bytes for a reserved which be used to fill zero
  if ( sizeof( digest ) > ( 6 + n + 1 ) )
  {
    strcpy( digest, "Basic " );
    esp_crypto_base64_encode( ( unsigned char * )digest + 6, n, ( size_t * )&out, ( const unsigned char * )user_info, strlen( user_info ) );
  }

  return digest;
}

//-----------------------------------------------------------------------------
static esp_err_t basic_auth_get_handler( httpd_req_t *req )
{
  basic_auth_info_t *basic_auth_info = req->user_ctx;

  size_t buf_len = httpd_req_get_hdr_value_len( req, "Authorization" ) + 1;
  if ( ( buf_len > 1 ) && ( buf_len <= sizeof( auth_buffer ) ) )
  {
    if ( httpd_req_get_hdr_value_str( req, "Authorization", auth_buffer, buf_len ) == ESP_OK )
    {
      char *auth_credentials = http_auth_basic( basic_auth_info->username, basic_auth_info->password );
      if ( !strncmp( auth_credentials, auth_buffer, buf_len ) )
      {
        ESP_LOGI(TAG,  "Authenticated!\n" );
        httpd_resp_set_status( req, HTTPD_200 );
        httpd_resp_set_hdr( req, "Connection", "keep-alive" );
        httpd_resp_send( req, html_ota, strlen( html_ota ) );
        return ESP_OK;
      }
    }
  }

  ESP_LOGI(TAG,  "Not authenticated\n" );
  httpd_resp_set_status( req, HTTPD_401 );
  httpd_resp_set_hdr( req, "Connection", "keep-alive" );
  httpd_resp_set_hdr( req, "WWW-Authenticate", "Basic realm=\"Hello\"" );
  httpd_resp_send( req, NULL, 0 );

  return ESP_OK;
}

//-----------------------------------------------------------------------------
static esp_err_t ota_post_handler( httpd_req_t *req )
{
  char buf[256];
  httpd_resp_set_status( req, HTTPD_500 );    // Assume failure
  
  int ret, remaining = req->content_len;
  ESP_LOGI(TAG,  "Receiving\n" );
  
  esp_ota_handle_t update_handle = 0 ;
  const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
  const esp_partition_t *running          = esp_ota_get_running_partition();
  
  if ( update_partition == NULL )
  {
    ESP_LOGI(TAG,  "Uh oh, bad things\n" );
    goto return_failure;
  }

  ESP_LOGI(TAG,  "Writing partition: type %d, subtype %d, offset 0x%08x\n", update_partition-> type, update_partition->subtype, update_partition->address);
  ESP_LOGI(TAG,  "Running partition: type %d, subtype %d, offset 0x%08x\n", running->type,           running->subtype,          running->address);
  esp_err_t err = ESP_OK;
  err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
  if (err != ESP_OK)
  {
      ESP_LOGI(TAG,  "esp_ota_begin failed (%s)", esp_err_to_name(err));
      goto return_failure;
  }
  while ( remaining > 0 )
  {
    // Read the data for the request
    if ( ( ret = httpd_req_recv( req, buf, MIN( remaining, sizeof( buf ) ) ) ) <= 0 )
    {
      if ( ret == HTTPD_SOCK_ERR_TIMEOUT )
      {
        // Retry receiving if timeout occurred
        continue;
      }

      goto return_failure;
    }
    
    size_t bytes_read = ret;
    
    remaining -= bytes_read;
    err = esp_ota_write( update_handle, buf, bytes_read);
    if (err != ESP_OK)
    {
      goto return_failure;
    }
  }

  ESP_LOGI(TAG,  "Receiving done\n" );

  // End response
  if ( ( esp_ota_end(update_handle)                   == ESP_OK ) && 
       ( esp_ota_set_boot_partition(update_partition) == ESP_OK ) )
  {
    ESP_LOGI(TAG,  "OTA Success?!\n Rebooting\n" );
    fflush( stdout );

    httpd_resp_set_status( req, HTTPD_200 );
    httpd_resp_send( req, NULL, 0 );
    
    vTaskDelay( 2000 / portTICK_RATE_MS);
    esp_restart();
    
    return ESP_OK;
  }
  ESP_LOGI(TAG,  "OTA End failed (%s)!\n", esp_err_to_name(err));

return_failure:
  if ( update_handle )
  {
    esp_ota_abort(update_handle);
  }

  httpd_resp_set_status( req, HTTPD_500 );    // Assume failure
  httpd_resp_send( req, NULL, 0 );
  return ESP_FAIL;
}

// Get handler for index page
// Just sends the index HTML file
static esp_err_t index_get_handler( httpd_req_t *req )
{
    httpd_resp_send(req, html_index, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Post handler for index page
// Depending on the content of the request:
// - Turns on/off the LED
// - Updates the wifi data
static esp_err_t index_post_handler( httpd_req_t *req )
{
    ESP_LOGI(TAG, "Received index POST request\n");

    char content[512];

    // Truncate if content length larger than the buffer
    size_t recv_size = MIN(req->content_len, sizeof(content));

    // Read content from post request
    int ret = httpd_req_recv(req, content, recv_size);
    if (ret <= 0) {  // 0 return value indicates connection closed
        // Check if timeout occurred */
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            /* In case of timeout one can choose to retry calling
             * httpd_req_recv(), but to keep it simple, here we
             * respond with an HTTP 408 (Request Timeout) error */
            httpd_resp_send_408(req);
        }
        /* In case of error, returning ESP_FAIL will
         * ensure that the underlying socket is closed */
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Content: %s\n", content);

    char resp[256] = "";

    // Parse JSON data
    jsmn_parser json_parser;
    jsmntok_t json_content[32];
    int num_tokens;

    jsmn_init(&json_parser);
    num_tokens = jsmn_parse(&json_parser, content, recv_size, json_content, 32);

    int token_len = 0;
    char* token_str = NULL;

    if (num_tokens < 2) {
        ESP_LOGI(TAG, "Error parsing JSON data");
    }
    else {
        token_len = json_content[1].end - json_content[1].start;
        token_str = (char*)malloc((token_len + 1) * sizeof(char));
        strncpy(token_str, content+json_content[1].start, token_len);
        token_str[token_len] = '\0';

        if (strcmp(token_str, "light") == 0) {
            if (num_tokens != 5) {
                ESP_LOGI(TAG, "Wrong number of tokens for light message!");
            }
            else {
                token_len = json_content[2].end - json_content[2].start;
                token_str = (char*)realloc(token_str, (token_len + 1) * sizeof(char));
                strncpy(token_str, content+json_content[2].start, token_len);
                token_str[token_len] = '\0';

                int light_num = atoi(token_str);

                if (light_num > 3 || light_num < 0) {
                    ESP_LOGI(TAG, "Light number %d out of range! Must be 0-3", light_num);
                }
                else {
                    token_len = json_content[3].end - json_content[3].start;
                    token_str = (char*)realloc(token_str, (token_len + 1) * sizeof(char));
                    strncpy(token_str, content+json_content[3].start, token_len);
                    token_str[token_len] = '\0';

                    if (strcmp(token_str, "val") != 0) {
                        ESP_LOGI(TAG, "Found light, but no value token");
                    }
                    else {
                        token_len = json_content[4].end - json_content[4].start;
                        token_str = (char*)realloc(token_str, (token_len + 1) * sizeof(char));
                        strncpy(token_str, content+json_content[4].start, token_len);
                        token_str[token_len] = '\0';

                        int light_val = atoi(token_str);

                        if (light_val < 0 || light_val > 255) {
                            ESP_LOGI(TAG, "Light value %d out of range! Must be 0-255", light_val);
                        }
                        else {
                            lights_set_brightness(light_val, light_num);
                            light_data[light_num].duty_cycle = light_val;
                            sprintf(resp, "Light updated");
                        }
                    }
                }
            }
        }
        else if (strcmp(token_str, "ssid") == 0) {
            if (num_tokens != 5) {
                ESP_LOGI(TAG, "Wrong number of tokens for Wifi message!");
            }
            else {
                token_len = json_content[2].end - json_content[2].start;
                token_str = (char*)realloc(token_str, (token_len + 1) * sizeof(char));
                strncpy(token_str, content+json_content[2].start, token_len);
                token_str[token_len] = '\0';

                if (token_len < 1 || token_len > 32) {
                    ESP_LOGI(TAG, "SSID is too long. Max 32 characters");
                }
                else {
                    char new_ssid[WIFI_SSID_LENGTH];
                    strcpy(new_ssid, token_str);

                    token_len = json_content[3].end - json_content[3].start;
                    token_str = (char*)realloc(token_str, (token_len + 1) * sizeof(char));
                    strncpy(token_str, content+json_content[3].start, token_len);
                    token_str[token_len] = '\0';

                    if (strcmp(token_str, "psk") != 0) {
                        ESP_LOGI(TAG, "Found ssid, but no password token");
                    }
                    else {
                        token_len = json_content[4].end - json_content[4].start;
                        token_str = (char*)realloc(token_str, (token_len + 1) * sizeof(char));
                        strncpy(token_str, content+json_content[4].start, token_len);
                        token_str[token_len] = '\0';

                        if (token_len < 8 || token_len > 63) {
                            ESP_LOGI(TAG, "Password is wrong length. Min 8 characters. Max 63 characters");
                        }
                        else {
                            // If wifi info is ok, save it to the global variables,
                            // save it to NVS, and set the new_wifi_info flag to trigger a reconnect
                            ESP_LOGI(TAG, "New SSID: %s", new_ssid);
                            ESP_LOGI(TAG, "New PSK: %s", token_str);
                            strcpy(esp_wifi_sta_ssid, new_ssid);
                            strcpy(esp_wifi_sta_pass, token_str);
                            save_wifi_info_to_nvs();
                            new_wifi_info = 1;
                            sprintf(resp, "New SSID and Password set! Connecting now");
                        }
                    }
                }
            }
        }
        else if (strcmp(token_str, "mqtt_broker") == 0) {
            if (num_tokens != 3) {
                ESP_LOGI(TAG, "Wrong number of tokens for MQTT message!");
            }
            else {
                token_len = json_content[2].end - json_content[2].start;
                token_str = (char*)realloc(token_str, (token_len + 1) * sizeof(char));
                strncpy(token_str, content+json_content[2].start, token_len);
                token_str[token_len] = '\0';

                if (token_len > 256) {
                    ESP_LOGI(TAG, "MQTT Broker URI too long. Must be 256 characters or less");
                }
                else {
                    strcpy(mqtt_broker_uri, token_str);
                    ESP_LOGI(TAG, "MQTT Broker set!");
                    sprintf(resp, "MQTT Broker Set!");
                }
            }
        }
        else if (strcmp(token_str, "light0_name") == 0) {
            if (num_tokens != 17) {
                ESP_LOGI(TAG, "Wrong number of tokens for light setup message!");
            }
            char cmp_str[12];
            uint8_t index;
            for (int i = 0; i < 4; i++){
                index = (i * 4) + 1;
                token_len = json_content[index].end - json_content[index].start;
                token_str = (char*)realloc(token_str, (token_len + 1) * sizeof(char));
                strncpy(token_str, content+json_content[index].start, token_len);
                token_str[token_len] = '\0';

                sprintf(cmp_str, "light%d_name", i);
                if (strcmp(cmp_str, token_str) != 0) {
                    ESP_LOGI(TAG, "Token doesn't match: %s", token_str);
                    sprintf(resp, "Error saving data");
                    break;
                }

                index = (i * 4) + 2;
                token_len = json_content[index].end - json_content[index].start;
                if (token_len > 0) {
                    token_str = (char*)realloc(token_str, (token_len + 1) * sizeof(char));
                    strncpy(token_str, content+json_content[index].start, token_len);
                    token_str[token_len] = '\0';

                    if (token_len > 12) {
                        ESP_LOGI(TAG, "Name too long. Max 12 chars: %s", token_str);
                        sprintf(resp, "Error saving data");
                        break;
                    }
                    strcpy(light_data[i].name, token_str);
                }
                else {
                    strcpy(light_data[i].name, "");
                }

                index = (i * 4) + 3;
                token_len = json_content[index].end - json_content[index].start;
                token_str = (char*)realloc(token_str, (token_len + 1) * sizeof(char));
                strncpy(token_str, content+json_content[index].start, token_len);
                token_str[token_len] = '\0';

                sprintf(cmp_str, "light%d_en", i);
                if (strcmp(cmp_str, token_str) != 0) {
                    ESP_LOGI(TAG, "Token doesn't match: %s", token_str);
                    sprintf(resp, "Error saving data");
                    break;
                }

                index = (i * 4) + 4;
                token_len = json_content[index].end - json_content[index].start;
                token_str = (char*)realloc(token_str, (token_len + 1) * sizeof(char));
                strncpy(token_str, content+json_content[index].start, token_len);
                token_str[token_len] = '\0';

                if (strcmp(token_str, "true") == 0) {
                    light_data[i].enabled = 1;
                }
                else if (strcmp(token_str, "false") == 0) {
                    light_data[i].enabled = 0;
                    light_data[i].duty_cycle = 0;
                    lights_set_brightness(0, i);
                    if (i == 3) {
                        sprintf(resp, "Data saved!");
                    }
                }
                else {
                    ESP_LOGI(TAG, "Invalid enabled string. Should be \"true\" or \"false\": %s", token_str);
                    sprintf(resp, "Error saving data");
                    break;
                }
            }
        }
        else {
            ESP_LOGI(TAG, "JSON token not recognized: %s", token_str);
        }
    }

    free(token_str);
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t status_update_handler( httpd_req_t *req )
{
    char json_data[1024];
    sprintf(json_data, 
"{\"lights\":\
{\
\"light0\":\
{\
\"name\": \"%s\",\
\"enabled\": \"%d\",\
\"duty_cycle\": \"%d\"\
},\
\"light1\":\
{\
\"name\": \"%s\",\
\"enabled\": \"%d\",\
\"duty_cycle\": \"%d\"\
},\
\"light2\":\
{\
\"name\": \"%s\",\
\"enabled\": \"%d\",\
\"duty_cycle\": \"%d\"\
},\
\"light3\":\
{\
\"name\": \"%s\",\
\"enabled\": \"%d\",\
\"duty_cycle\": \"%d\"\
}\
}\
}",
        light_data[0].name,
        light_data[0].enabled,
        light_data[0].duty_cycle,
        light_data[1].name,
        light_data[1].enabled,
        light_data[1].duty_cycle,
        light_data[2].name,
        light_data[2].enabled,
        light_data[2].duty_cycle,
        light_data[3].name,
        light_data[3].enabled,
        light_data[3].duty_cycle);
    ESP_LOGI(TAG,  "Sending update. JSON length: %d", strlen(json_data));
    httpd_resp_send(req, json_data, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Starts the webserver after wifi is connected or AP mode is started
static httpd_handle_t start_webserver( void )
{
  httpd_handle_t server = NULL;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.lru_purge_enable = true;

  // Start the httpd server
  ESP_LOGI(TAG,  "Starting server on port %d\n", config.server_port );

  if ( httpd_start( &server, &config ) == ESP_OK )
  {
    static const httpd_uri_t ota =
    {
      .uri       = "/ota",
      .method    = HTTP_POST,
      .handler   = ota_post_handler,
      .user_ctx  = NULL
    };
    httpd_register_uri_handler( server, &ota );
    
    static httpd_uri_t root =
    {
      .uri       = "/ota",
      .method    = HTTP_GET,
      .handler   = basic_auth_get_handler,
      .user_ctx  = &auth_info,
    };
    httpd_register_uri_handler( server, &root );

    static httpd_uri_t index =
    {
      .uri       = "/",
      .method    = HTTP_GET,
      .handler   = index_get_handler,
      .user_ctx  = NULL
    };
    httpd_register_uri_handler( server, &index );

    static httpd_uri_t index_post =
    {
      .uri       = "/",
      .method    = HTTP_POST,
      .handler   = index_post_handler,
      .user_ctx  = NULL
    };
    httpd_register_uri_handler( server, &index_post );

    static httpd_uri_t status_update =
    {
      .uri       = "/status_update",
      .method    = HTTP_GET,
      .handler   = status_update_handler,
      .user_ctx  = NULL
    };
    httpd_register_uri_handler( server, &status_update );

  }
    
  return NULL;
}

// Interrupt to start connecting to Wifi once the wifi station mode is started
static void sta_start_handler( void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data )
{
    ESP_LOGI(TAG, "Connecting to WIFI");
    esp_wifi_connect();
}

// If wifi disconnects, stop the webserver if it's running and retry to connect
static void disconnect_handler( void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data )
{
    httpd_handle_t *server = ( httpd_handle_t * ) arg;

    if ( *server )
    {
        ESP_LOGI(TAG,  "Stopping webserver" );
        httpd_stop( *server );
        *server = NULL;
    }

    if (wifi_retry_count < ESP_MAXIMUM_CONNECT_RETRY) {
        esp_wifi_connect();
        wifi_retry_count++;
        ESP_LOGI(TAG, "Retry to connect to the AP");
    } else {
        wifi_connected = 0;
        ESP_LOGI(TAG, "Failed to connect to Wifi");
    }

}

// If wifi connects, set the wifi_connected flag and start the webserver
static void connect_handler( void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data )
{
    ESP_LOGI(TAG, "Connected!\n");
    wifi_connected = 1;
    httpd_handle_t *server = ( httpd_handle_t * ) arg;

    if ( *server == NULL )
    {
        ESP_LOGI(TAG,  "Starting webserver" );
        *server = start_webserver();
    }
}

// Wifi AP event handler. Just logs debug info when wifi stations connect/disconnect
static void wifi_ap_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

// Initializes mDNS service to set a hostname for the esp32
static void initialise_mdns(void)
{

    //initialize mDNS service
    esp_err_t err = mdns_init();
    if (err) {
        ESP_LOGI(TAG, "MDNS Init failed: %d\n", err);
        return;
    }
    //set mDNS hostname (required if you want to advertise services)
    mdns_hostname_set(ESP_HOSTNAME);
    ESP_LOGI(TAG, "mdns hostname set to: [%s]", ESP_HOSTNAME);
    //set default mDNS instance name
    mdns_instance_name_set(ESP_HOSTNAME);
}

// The wifi task intializes the wifi interface and attempts to connect in station
// mode if wifi info is saved. If no info is saved or the connection fails, it
// defaults back to AP mode. If new wifi data is entered, it will attempt to 
// connect in station mode again
static void wifi_task( void *Param )
{
    ESP_LOGI(TAG,  "Wifi task starting\n" );

    // Variable to store mac address string which is unique to every ESP
    char mac_addr_str[13];
    //Read MAC address
    uint8_t mac_addr[8];
    ESP_ERROR_CHECK(esp_read_mac(mac_addr, ESP_MAC_WIFI_STA));
    sprintf(mac_addr_str, "%02x%02x%02x%02x%02x%02x", mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    ESP_LOGI(TAG,  "ESP MAC address: %s", mac_addr_str);
  
    httpd_handle_t server = NULL;
  
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    initialise_mdns();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers to stop the server when Wi-Fi is disconnected, and re-start it upon connection
    esp_event_handler_instance_t instance_sta_start;
    esp_event_handler_instance_t instance_connected;
    esp_event_handler_instance_t instance_disconnected;
    esp_event_handler_instance_t instance_ap_handler;
    ESP_ERROR_CHECK(esp_event_handler_instance_register( WIFI_EVENT, WIFI_EVENT_STA_START, &sta_start_handler, NULL, &instance_sta_start ));
    ESP_ERROR_CHECK(esp_event_handler_instance_register( IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &server, &instance_connected ));
    ESP_ERROR_CHECK(esp_event_handler_instance_register( WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server, &instance_disconnected ));
    ESP_ERROR_CHECK(esp_event_handler_instance_register( WIFI_EVENT, WIFI_EVENT_AP_STACONNECTED, &wifi_ap_handler, NULL, &instance_ap_handler));

    wifi_config_t wifi_sta_config = {
        .sta = {
            //.ssid = esp_wifi_sta_ssid,
            //.password = esp_wifi_sta_pass,
            /* Setting the threshold to WPA2 means the ESP will only connect to
            networks with WPA2 security or stronger */
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    memcpy(wifi_sta_config.sta.ssid, esp_wifi_sta_ssid, 32);
    memcpy(wifi_sta_config.sta.password, esp_wifi_sta_pass, 64);

    

    wifi_config_t wifi_ap_config = {
        .ap = {
            .ssid = ESP_WIFI_AP_SSID,
            .ssid_len = strlen(ESP_WIFI_AP_SSID),
            .channel = ESP_WIFI_AP_CHANNEL,
            .password = ESP_WIFI_AP_PASS,
            .max_connection = ESP_WIFI_AP_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
        },
    };
    if (strlen(ESP_WIFI_AP_PASS) == 0) {
        wifi_ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    // Append MAC address to end of AP SSID name
    char ap_ssid_name[33];
    sprintf(ap_ssid_name, "%s_%x%x%x%x%x%x", ESP_WIFI_AP_SSID, mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    memcpy(wifi_ap_config.ap.ssid, ap_ssid_name, 32);
    wifi_ap_config.ap.ssid_len = strlen(ap_ssid_name);

    if (strcmp(esp_wifi_sta_ssid, "") != 0) {
        ESP_LOGI(TAG, "Wifi info detected. Starting in STA mode");

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config) );
        ESP_ERROR_CHECK(esp_wifi_start() );

        ESP_LOGI(TAG, "wifi_init_sta finished");

        vTaskDelay(ESP_WIFI_CONNECT_WAIT / portTICK_RATE_MS);
    }
    else {
        ESP_LOGI(TAG, "No Wifi info detected");
    }
    
    const uint32_t task_delay_ms = 1000;
    int second_counter = 0;
    while(1) {
        if (wifi_connected == 0) {
            ESP_LOGI(TAG, "Wifi Disconnected! Switching to AP mode");
            ap_mode = 1;
            ESP_ERROR_CHECK(esp_wifi_stop() );
            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP) );
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config) );
            ESP_ERROR_CHECK(esp_wifi_start() );
            if (server == NULL) {
                ESP_LOGI(TAG,  "Starting webserver");
                server = start_webserver();
            }
            wifi_connected = 1;
            second_counter = 0;
        }
        if (new_wifi_info == 1) {
            ap_mode = 0;
            wifi_connected = 0;
            wifi_retry_count = 0;
            ESP_LOGI(TAG, "New Wifi info detected. Switching to STA mode");
            if (server){
                ESP_LOGI(TAG,  "Stopping webserver" );
                httpd_stop(server);
                server = NULL;
            }
            memcpy(wifi_sta_config.sta.ssid, esp_wifi_sta_ssid, 32);
            memcpy(wifi_sta_config.sta.password, esp_wifi_sta_pass, 64);
            ESP_ERROR_CHECK(esp_wifi_stop() );
            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config) );
            ESP_ERROR_CHECK(esp_wifi_start() );
            new_wifi_info = 0;
            vTaskDelay(ESP_WIFI_CONNECT_WAIT / portTICK_RATE_MS);
        }
        if (second_counter >= 60) {
            second_counter = 0;
            ESP_LOGI(TAG, "60 second timer");
            if (strcmp(esp_wifi_sta_ssid, "") != 0 && ap_mode == 1) {
                ESP_LOGI(TAG, "Checking if any stations connected");
                wifi_sta_list_t station_list;
                if (esp_wifi_ap_get_sta_list(&station_list) == ESP_OK) {
                    ESP_LOGI(TAG, "%d stations connected", station_list.num);
                    if (station_list.num == 0) {
                        ESP_LOGI(TAG, "No stations connected to AP. Checking for wifi");
                        ap_mode = 0;
                        wifi_connected = 0;
                        wifi_retry_count = 0;
                        if (server){
                            ESP_LOGI(TAG,  "Stopping webserver" );
                            httpd_stop(server);
                            server = NULL;
                        }
                        memcpy(wifi_sta_config.sta.ssid, esp_wifi_sta_ssid, 32);
                        memcpy(wifi_sta_config.sta.password, esp_wifi_sta_pass, 64);
                        ESP_ERROR_CHECK(esp_wifi_stop() );
                        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
                        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config) );
                        ESP_ERROR_CHECK(esp_wifi_start() );
                        new_wifi_info = 0;
                        vTaskDelay(ESP_WIFI_CONNECT_WAIT / portTICK_RATE_MS);
                    }
                    else {
                        ESP_LOGI(TAG, "STA0 MAC: %x:%x:%x:%x:%x:%x", station_list.sta[0].mac[0], station_list.sta[0].mac[1], station_list.sta[0].mac[2], station_list.sta[0].mac[3], station_list.sta[0].mac[4], station_list.sta[0].mac[5]);
                    }
                }
            }
        }
        vTaskDelay(task_delay_ms / portTICK_RATE_MS);
        second_counter++;
    }
    
}

// OTA task was borrowed from another project
//-----------------------------------------------------------------------------
static void ota_task(void *Param)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if ( esp_ota_get_state_partition(running, &ota_state) == ESP_OK ) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            // Validate image some how, then call:
            esp_ota_mark_app_valid_cancel_rollback();
            // If needed: esp_ota_mark_app_invalid_rollback_and_reboot();
        }
    }
  
    const uint32_t task_delay_ms = 10;
    while(1) {
        vTaskDelay( task_delay_ms / portTICK_RATE_MS);
    }
}


void app_main( void )
{ 
    ESP_LOGI(TAG,  "****************************\n" );
    ESP_LOGI(TAG,  "Application task starting\n" );

    // Initialize NVS.
    esp_err_t error = nvs_flash_init();
    if ( ( error == ESP_ERR_NVS_NO_FREE_PAGES ) || ( error == ESP_ERR_NVS_NEW_VERSION_FOUND ) ) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        error = nvs_flash_init();
    }
    ESP_ERROR_CHECK( error );

    // Initialize LED outputs
    lights_ledc_init();

    sprintf(light_data[0].name, "Light 0");
    light_data[0].enabled = 1;
    light_data[0].duty_cycle = 0;
    sprintf(light_data[1].name, "Light 1");
    light_data[1].enabled = 1;
    light_data[1].duty_cycle = 0;
    sprintf(light_data[2].name, "Light 2");
    light_data[2].enabled = 1;
    light_data[2].duty_cycle = 0;
    sprintf(light_data[3].name, "Light 3");
    light_data[3].enabled = 1;
    light_data[3].duty_cycle = 0;

    // Initialize wifi info from NVS
    if (strcmp(esp_wifi_sta_ssid, "") == 0) {
        read_wifi_info_from_nvs();
    }
  
    // Start wifi and ota tasks
    xTaskCreate( wifi_task, "wifi_task", 4096, NULL, 0, NULL );
    xTaskCreate( ota_task, "ota_task", 8192, NULL, 5, NULL);
  
    const uint32_t task_delay_ms = 10;
    while(1) {
        vTaskDelay( task_delay_ms / portTICK_RATE_MS);
        fflush(stdout);
    }
}
