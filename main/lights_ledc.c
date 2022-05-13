#include "lights_ledc.h"
#include "driver/ledc.h"

#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE // ESP32-C3 only supports low speed mode
#define LEDC_OUTPUT_IO_0        (7) // Define the output GPIO7
#define LEDC_OUTPUT_IO_1        (6) // Define the output GPIO6
#define LEDC_OUTPUT_IO_2        (5) // Define the output GPIO5
#define LEDC_OUTPUT_IO_3        (4) // Define the output GPIO4
#define LEDC_DUTY_RES           LEDC_TIMER_8_BIT // Set duty resolution to 8 bits
#define LEDC_FREQUENCY          (25000) // Frequency in Hertz. Set frequency at 25 kHz
#define LEDC_FADE_TIME          (250) // 250ms

void lights_ledc_init(void)
{
    // Prepare and then apply the LEDC PWM timer configuration
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = LEDC_FREQUENCY,  // Set output frequency at 5 kHz
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t ledc_channel_0 = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL_0,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LEDC_OUTPUT_IO_0,
        .duty           = 0, // Set duty to 0%
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel_0));

    ledc_channel_config_t ledc_channel_1 = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL_1,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LEDC_OUTPUT_IO_1,
        .duty           = 0, // Set duty to 0%
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel_1));

    ledc_channel_config_t ledc_channel_2 = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL_2,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LEDC_OUTPUT_IO_2,
        .duty           = 0, // Set duty to 0%
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel_2));

    ledc_channel_config_t ledc_channel_3 = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL_3,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LEDC_OUTPUT_IO_3,
        .duty           = 0, // Set duty to 0%
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel_3));

    ESP_ERROR_CHECK(ledc_fade_func_install(0));
}

void lights_set_brightness(int pwm, int channel)
{
    if (channel == 0) {
        uint32_t duty_to_fade = ledc_get_duty(LEDC_MODE, LEDC_CHANNEL_0);
        duty_to_fade = (abs(duty_to_fade - pwm) * LEDC_FADE_TIME) / 255;
        ledc_set_fade_with_time(LEDC_MODE, LEDC_CHANNEL_0, pwm, duty_to_fade);
        ledc_fade_start(LEDC_MODE, LEDC_CHANNEL_0, LEDC_FADE_NO_WAIT);
    }
    else if (channel == 1) {
        uint32_t duty_to_fade = ledc_get_duty(LEDC_MODE, LEDC_CHANNEL_1);
        duty_to_fade = (abs(duty_to_fade - pwm) * LEDC_FADE_TIME) / 255;
        ledc_set_fade_with_time(LEDC_MODE, LEDC_CHANNEL_1, pwm, duty_to_fade);
        ledc_fade_start(LEDC_MODE, LEDC_CHANNEL_1, LEDC_FADE_NO_WAIT);
    }
    else if (channel == 2) {
        uint32_t duty_to_fade = ledc_get_duty(LEDC_MODE, LEDC_CHANNEL_2);
        duty_to_fade = (abs(duty_to_fade - pwm) * LEDC_FADE_TIME) / 255;
        ledc_set_fade_with_time(LEDC_MODE, LEDC_CHANNEL_2, pwm, duty_to_fade);
        ledc_fade_start(LEDC_MODE, LEDC_CHANNEL_2, LEDC_FADE_NO_WAIT);
    }
    else if (channel == 3) {
        uint32_t duty_to_fade = ledc_get_duty(LEDC_MODE, LEDC_CHANNEL_3);
        duty_to_fade = (abs(duty_to_fade - pwm) * LEDC_FADE_TIME) / 255;
        ledc_set_fade_with_time(LEDC_MODE, LEDC_CHANNEL_3, pwm, duty_to_fade);
        ledc_fade_start(LEDC_MODE, LEDC_CHANNEL_3, LEDC_FADE_NO_WAIT);
    }
}
