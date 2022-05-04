#ifndef LIGHTS_LEDC_H_INCLUDED
#define LIGHTS_LEDC_H_INCLUDED

void lights_ledc_init(void);
void lights_set_brightness(int pwm, int channel);

#endif