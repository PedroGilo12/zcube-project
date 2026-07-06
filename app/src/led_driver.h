#ifndef LED_DRIVER_H
#define LED_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
	uint8_t r;
	uint8_t g;
	uint8_t b;
} led_color_t;

typedef enum {
	LED_EFFECT_OFF = 0,
	LED_EFFECT_SOLID,
	LED_EFFECT_BLINK,
	LED_EFFECT_FADE,
	LED_EFFECT_STROBE
} led_effect_t;

typedef struct {
	led_effect_t effect;
	led_color_t color1;
	led_color_t color2;
	led_color_t current_color;
	uint32_t period_ms;
	uint32_t elapsed_ms;
	bool state_flag;
} led_channel_t;

int led_driver_init(void);
void led_driver_set_effect(uint32_t index, led_effect_t effect, led_color_t c1, led_color_t c2,
			   uint32_t period_ms);
void led_driver_set_color(uint32_t index, led_color_t color);
void led_driver_set_all(led_effect_t effect, led_color_t c1, led_color_t c2, uint32_t period_ms);
void led_driver_update(uint32_t delta_ms);

#endif
