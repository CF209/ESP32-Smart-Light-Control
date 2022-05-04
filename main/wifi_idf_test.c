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

#define EXAMPLE_ESP_WIFI_SSID      "Pixel_5173"
#define EXAMPLE_ESP_WIFI_PASS      "password"
#define EXAMPLE_ESP_MAXIMUM_RETRY  10

#define ESP_HOSTNAME    "my-esp32"

static int s_retry_num = 0;

typedef struct
{
  const char *username;
  const char *password;
} basic_auth_info_t;

#define HTTPD_401      "401 UNAUTHORIZED"           /*!< HTTP Response 401 */

static basic_auth_info_t auth_info = 
{
  .username = "cfarrah",
  .password = "letmein",
};

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
char auth_buffer[512];
const char html_post_file[] = "\
<style>\n\
.progress {margin: 15px auto;  max-width: 500px;height: 30px;}\n\
.progress .progress__bar {\n\
  height: 100%; width: 1%; border-radius: 15px;\n\
  background: repeating-linear-gradient(135deg,#336ffc,#036ffc 15px,#1163cf 15px,#1163cf 30px); }\n\
 .status {font-weight: bold; font-size: 30px;};\n\
</style>\n\
<link rel=\"stylesheet\" href=\"https://cdnjs.cloudflare.com/ajax/libs/twitter-bootstrap/2.2.1/css/bootstrap.min.css\">\n\
<div class=\"well\" style=\"text-align: center;\">\n\
  <div class=\"btn\" onclick=\"file_sel.click();\"><i class=\"icon-upload\" style=\"padding-right: 5px;\"></i>Upload Firmware</div>\n\
  <div class=\"progress\"><div class=\"progress__bar\" id=\"progress\"></div></div>\n\
  <div class=\"status\" id=\"status_div\"></div>\n\
</div>\n\
<input type=\"file\" id=\"file_sel\" onchange=\"upload_file()\" style=\"display: none;\">\n\
<script>\n\
function upload_file() {\n\
  document.getElementById(\"status_div\").innerHTML = \"Upload in progress\";\n\
  let data = document.getElementById(\"file_sel\").files[0];\n\
  xhr = new XMLHttpRequest();\n\
  xhr.open(\"POST\", \"/ota\", true);\n\
  xhr.setRequestHeader('X-Requested-With', 'XMLHttpRequest');\n\
  xhr.upload.addEventListener(\"progress\", function (event) {\n\
     if (event.lengthComputable) {\n\
    	 document.getElementById(\"progress\").style.width = (event.loaded / event.total) * 100 + \"%\";\n\
     }\n\
  });\n\
  xhr.onreadystatechange = function () {\n\
    if(xhr.readyState === XMLHttpRequest.DONE) {\n\
      var status = xhr.status;\n\
      if (status >= 200 && status < 400)\n\
      {\n\
        document.getElementById(\"status_div\").innerHTML = \"Upload accepted. Device will reboot.\";\n\
      } else {\n\
        document.getElementById(\"status_div\").innerHTML = \"Upload rejected!\";\n\
      }\n\
    }\n\
  };\n\
  xhr.send(data);\n\
  return false;\n\
}\n\
</script>";

const char html_index[] = "\
<!DOCTYPE HTML><html style=\"background-color: skyblue; text-align: center;\">\n\
<h1>ESP32 Wifi Test</h1>\n\
<body>\n\
   <form onsubmit=\"led_on()\"><input type=\"submit\" value=\"Click here to turn ON the LED\"></form><br>\n\
   <form onsubmit=\"led_off()\"><input type=\"submit\" value=\"Click here to turn OFF the LED\"></form><br>\n\
   <br>\n\
   <br>\n\
   <div>Change the Wifi network:</div><br>\n\
   <form action=\"/WIFI\">\n\
      SSID: <input type=\"text\" name=\"ssid\">\n\
      <br>\n\
      Password: <input type=\"text\" name=\"password\">\n\
      <br>\n\
      <input type=\"submit\" value=\"Connect\">\n\
   </form>\n\
   <br>\n\
   <br>\n\
   <form action=\"/ota\"><input type=\"submit\" value=\"Update Firmware\"></form><br>\n\
<script>\n\
function led_on() {\n\
  xhr = new XMLHttpRequest();\n\
  xhr.open('POST', '/', true);\n\
  xhr.setRequestHeader('X-Requested-With', 'XMLHttpRequest');\n\
  xhr.send('H');\n\
};\n\
function led_off() {\n\
  xhr = new XMLHttpRequest();\n\
  xhr.open('POST', '/', true);\n\
  xhr.setRequestHeader('X-Requested-With', 'XMLHttpRequest');\n\
  xhr.send('L');\n\
};\n\
</script>\n\
</body>\n\
</html>";

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
        printf( "Authenticated!\n" );
        httpd_resp_set_status( req, HTTPD_200 );
        httpd_resp_set_hdr( req, "Connection", "keep-alive" );
        httpd_resp_send( req, html_post_file, strlen( html_post_file ) );
        return ESP_OK;
      }
    }
  }

  printf( "Not authenticated\n" );
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
  printf( "Receiving\n" );
  
  esp_ota_handle_t update_handle = 0 ;
  const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
  const esp_partition_t *running          = esp_ota_get_running_partition();
  
  if ( update_partition == NULL )
  {
    printf( "Uh oh, bad things\n" );
    goto return_failure;
  }

  printf( "Writing partition: type %d, subtype %d, offset 0x%08x\n", update_partition-> type, update_partition->subtype, update_partition->address);
  printf( "Running partition: type %d, subtype %d, offset 0x%08x\n", running->type,           running->subtype,          running->address);
  esp_err_t err = ESP_OK;
  err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
  if (err != ESP_OK)
  {
      printf( "esp_ota_begin failed (%s)", esp_err_to_name(err));
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

  printf( "Receiving done\n" );

  // End response
  if ( ( esp_ota_end(update_handle)                   == ESP_OK ) && 
       ( esp_ota_set_boot_partition(update_partition) == ESP_OK ) )
  {
    printf( "OTA Success?!\n Rebooting\n" );
    fflush( stdout );

    httpd_resp_set_status( req, HTTPD_200 );
    httpd_resp_send( req, NULL, 0 );
    
    vTaskDelay( 2000 / portTICK_RATE_MS);
    esp_restart();
    
    return ESP_OK;
  }
  printf( "OTA End failed (%s)!\n", esp_err_to_name(err));

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
esp_err_t http_404_error_handler( httpd_req_t *req, httpd_err_code_t err )
{
  if ( strcmp( "/hello", req->uri ) == 0 )
  {
    httpd_resp_send_err( req, HTTPD_404_NOT_FOUND, "/hello URI is not available" );
    return ESP_OK;    // Return ESP_OK to keep underlying socket open
  }
  else if ( strcmp( "/echo", req->uri ) == 0 )
  {
    httpd_resp_send_err( req, HTTPD_404_NOT_FOUND, "/echo URI is not available" );    
    return ESP_FAIL;    // Return ESP_FAIL to close underlying socket
  }
  
  httpd_resp_send_err( req, HTTPD_404_NOT_FOUND, "404 error" );
  return ESP_FAIL;  // For any other URI send 404 and close socket
}

//-----------------------------------------------------------------------------
static esp_err_t index_handler( httpd_req_t *req )
{
    httpd_resp_send(req, html_index, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t led_handler( httpd_req_t *req )
{
    printf("Received LED POST request\n");
    char content[100];

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

    printf("Content: %s\n", content);

    if (content[0] == 'H') {
        lights_set_brightness(255, 0);
        lights_set_brightness(255, 1);
    }
    else if (content[0] == 'L') {
        lights_set_brightness(0, 0);
        lights_set_brightness(0, 1);
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
  printf( "Starting server on port %d\n", config.server_port );

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
    printf("Connecting to WIFI");
    esp_wifi_connect();
}

//-----------------------------------------------------------------------------
static void disconnect_handler( void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data )
{
    httpd_handle_t *server = ( httpd_handle_t * ) arg;

    if ( *server )
    {
        printf( "Stopping webserver" );
        httpd_stop( *server );
        *server = NULL;
    }

    if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
        esp_wifi_connect();
        s_retry_num++;
        printf("Retry to connect to the AP");
    } else {
        printf("Failed to connect to Wifi");
        esp_restart();
    }

}

//-----------------------------------------------------------------------------
static void connect_handler( void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data )
{
    printf("Connected!\n");
    httpd_handle_t *server = ( httpd_handle_t * ) arg;

    if ( *server == NULL )
    {
        printf( "Starting webserver" );
        *server = start_webserver();
    }
}

static void initialise_mdns(void)
{

    //initialize mDNS service
    esp_err_t err = mdns_init();
    if (err) {
        printf("MDNS Init failed: %d\n", err);
        return;
    }
    //set mDNS hostname (required if you want to advertise services)
    mdns_hostname_set(ESP_HOSTNAME);
    printf("mdns hostname set to: [%s]", ESP_HOSTNAME);
    //set default mDNS instance name
    mdns_instance_name_set(ESP_HOSTNAME);
}

//-----------------------------------------------------------------------------
static void wifi_task( void *Param )
{
    printf( "Wifi task starting\n" );
  
    httpd_handle_t server = NULL;
  
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    initialise_mdns();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers to stop the server when Wi-Fi is disconnected, and re-start it upon connection
    esp_event_handler_instance_t instance_sta_start;
    esp_event_handler_instance_t instance_connected;
    esp_event_handler_instance_t instance_disconnected;
    ESP_ERROR_CHECK(esp_event_handler_instance_register( WIFI_EVENT, WIFI_EVENT_STA_START, &sta_start_handler, NULL, &instance_sta_start ));
    ESP_ERROR_CHECK(esp_event_handler_instance_register( IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &server, &instance_connected ));
    ESP_ERROR_CHECK(esp_event_handler_instance_register( WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server, &instance_disconnected ));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            /* Setting the threshold to WPA2 means the ESP will only connect to
            networks with WPA2 security or stronger */
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    printf("wifi_init_sta finished");
    
    const uint32_t task_delay_ms = 10;
    while(1) {
        vTaskDelay( task_delay_ms / portTICK_RATE_MS);
    }
    
}

//-----------------------------------------------------------------------------
static void ota_task(void *Param)
{
//  #define HASH_LEN 32         // SHA-256 digest length
//  uint8_t sha_256[HASH_LEN] = { 0 };
//  esp_partition_t partition;
// 
//  partition.address   = ESP_PARTITION_TABLE_OFFSET;
//  partition.size      = ESP_PARTITION_TABLE_MAX_LEN;
//  partition.type      = ESP_PARTITION_TYPE_DATA;
//  esp_partition_get_sha256(&partition, sha_256);
// 
//  partition.address   = ESP_BOOTLOADER_OFFSET;
//  partition.size      = ESP_PARTITION_TABLE_OFFSET;
//  partition.type      = ESP_PARTITION_TYPE_APP;
//  esp_partition_get_sha256(&partition, sha_256);
// 
//  esp_partition_get_sha256(esp_ota_get_running_partition(), sha_256);
  
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
    printf( "****************************\n" );
    printf( "Application task starting\n" );

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
