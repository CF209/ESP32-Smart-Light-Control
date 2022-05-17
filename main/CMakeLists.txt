idf_component_register( SRCS "esp_smart_lights.c" "lights_ledc.c" "nvs_data.c" "jsmn.h"
                        EMBED_TXTFILES "index.html" "ota.html"
                        INCLUDE_DIRS "." )
