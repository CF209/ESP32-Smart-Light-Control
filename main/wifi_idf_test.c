#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_ota_ops.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"
#include "esp_tls_crypto.h"
#include <esp_http_server.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_err.h"
#include "mdns.h"

#include "lights_ledc.h"

#define ESP_MAXIMUM_CONNECT_RETRY  10
#define ESP_WIFI_CONNECT_WAIT      15000
#define ESP_WIFI_AP_SSID           "esp_wifi_network"
#define ESP_WIFI_AP_PASS           "password"
#define ESP_WIFI_AP_CHANNEL        1
#define ESP_WIFI_AP_MAX_STA_CONN   4

#define ESP_HOSTNAME    "my-esp32"

static const char *TAG = "wifi idf test";

static int s_retry_num = 0;
static uint8_t wifi_connected = 0;
static uint8_t new_wifi_info = 0;

static char esp_wifi_sta_ssid[33] = "Pixel_5173";
static char esp_wifi_sta_pass[64] = "applesauce";

typedef struct
{
  const char *username;
  const char *password;
} basic_auth_info_t;

#define HTTPD_401      "401 UNAUTHORIZED"           /*!< HTTP Response 401 */

static basic_auth_info_t auth_info = 
{
  .username = "admin",
  .password = "admin",
};

char auth_buffer[512];

//Read HTML files
extern const char html_ota[] asm("_binary_ota_html_start");
extern const char html_index[] asm("_binary_index_html_start");

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

//-----------------------------------------------------------------------------
static esp_err_t index_handler( httpd_req_t *req )
{
    httpd_resp_send(req, html_index, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t led_handler( httpd_req_t *req )
{
    ESP_LOGI(TAG, "Received LED POST request\n");
    char content[200];

    /* Truncate if content length larger than the buffer */
    size_t recv_size = MIN(req->content_len, sizeof(content));

    int ret = httpd_req_recv(req, content, recv_size);
    if (ret <= 0) {  /* 0 return value indicates connection closed */
        /* Check if timeout occurred */
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

    if (content[0] == 'H') {
        lights_set_brightness(255, 0);
        lights_set_brightness(255, 1);
    }
    else if (content[0] == 'L') {
        lights_set_brightness(0, 0);
        lights_set_brightness(0, 1);
    }
    else if (strncmp(content, "{\"ssid\": \"", 10) == 0) {
        char* ptr = content + 10;
        uint8_t i = 0;
        char new_ssid[33];
        while (*ptr != '"' && i < 33) {
            new_ssid[i] = *ptr;
            i++;
            ptr++;
        }
        new_ssid[i] = '\0';
        if (*ptr != '"') {
            ESP_LOGI(TAG, "SSID is too long. Max 32 characters");
        }
        else if (strncmp(ptr, "\", \"psk\": \"", 11) == 0) {
            i = 0;
            ptr += 11;
            char new_pass[64];
            while (*ptr != '"' && i < 64) {
                new_pass[i] = *ptr;
                i++;
                ptr++;
            }
            new_pass[i] = '\0';
            if (*ptr != '"') {
                ESP_LOGI(TAG, "Password is too long. Max 63 characters");
            }
            else {
                ESP_LOGI(TAG, "New SSID: %s", new_ssid);
                ESP_LOGI(TAG, "New PSK: %s", new_pass);
                strcpy(esp_wifi_sta_ssid, new_ssid);
                strcpy(esp_wifi_sta_pass, new_pass);
                new_wifi_info = 1;
            }
        }
        else {
            ESP_LOGI(TAG, "Found SSID but no password");
        }
    }
    else {
        ESP_LOGI(TAG, "Put request not recognized");
    }
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

//-----------------------------------------------------------------------------
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
      .handler   = index_handler,
      .user_ctx  = NULL
    };
    httpd_register_uri_handler( server, &index );

    static httpd_uri_t index_post =
    {
      .uri       = "/",
      .method    = HTTP_POST,
      .handler   = led_handler,
      .user_ctx  = NULL
    };
    httpd_register_uri_handler( server, &index_post );

  }
    
  return NULL;
}

static void sta_start_handler( void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data )
{
    ESP_LOGI(TAG, "Connecting to WIFI");
    esp_wifi_connect();
}

//-----------------------------------------------------------------------------
static void disconnect_handler( void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data )
{
    httpd_handle_t *server = ( httpd_handle_t * ) arg;

    if ( *server )
    {
        ESP_LOGI(TAG,  "Stopping webserver" );
        httpd_stop( *server );
        *server = NULL;
    }

    if (s_retry_num < ESP_MAXIMUM_CONNECT_RETRY) {
        esp_wifi_connect();
        s_retry_num++;
        ESP_LOGI(TAG, "Retry to connect to the AP");
    } else {
        wifi_connected = 0;
        ESP_LOGI(TAG, "Failed to connect to Wifi");
    }

}

//-----------------------------------------------------------------------------
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

//-----------------------------------------------------------------------------
static void wifi_task( void *Param )
{
    ESP_LOGI(TAG,  "Wifi task starting\n" );
  
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
    while(1) {
        if (wifi_connected == 0) {
            ESP_LOGI(TAG, "Wifi Disconnected! Switching to AP mode");
            ESP_ERROR_CHECK(esp_wifi_stop() );
            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP) );
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config) );
            ESP_ERROR_CHECK(esp_wifi_start() );
            if (server == NULL) {
                ESP_LOGI(TAG,  "Starting webserver");
                server = start_webserver();
            }
            wifi_connected = 1;
        }
        if (new_wifi_info == 1) {
            wifi_connected = 0;
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
        vTaskDelay(task_delay_ms / portTICK_RATE_MS);
    }
    
}

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

//-----------------------------------------------------------------------------
void app_main( void )
{ 
    esp_err_t error;
    ESP_LOGI(TAG,  "****************************\n" );
    ESP_LOGI(TAG,  "Application task starting\n" );

    // Initialize NVS.
    error = nvs_flash_init();
    if ( ( error == ESP_ERR_NVS_NO_FREE_PAGES ) || ( error == ESP_ERR_NVS_NEW_VERSION_FOUND ) ) {
        // Don't bother checking return codes, it's not like we can do anything about failures here anyways
        nvs_flash_erase();
        nvs_flash_init();
    }

    lights_ledc_init();
  
    // Put all the wifi stuff in a separate task so that we don't have to wait for a connection
    xTaskCreate( wifi_task, "wifi_task", 4096, NULL, 0, NULL );
    xTaskCreate( ota_task, "ota_task", 8192, NULL, 5, NULL);
  
    const uint32_t task_delay_ms = 10;
    while(1) {
        vTaskDelay( task_delay_ms / portTICK_RATE_MS);
        fflush(stdout);
    }
}
