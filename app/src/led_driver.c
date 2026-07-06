/**
 * @file led_driver.c
 *
 * @author Pedro Giló (phvg@ic.ufal.br)
 * @author Thiago Laurentino (tfml@ic.ufal.br)
 * @author Caio Oliveira (cofa@ic.ufal.br)
 *
 * @brief Driver do LED WS2812 (I2S): motor de efeitos por canal e servico que
 *        segue a fase do Pomodoro pelo canal de UI (cor fixa por fase; branco
 *        quando pausado).
 * @version 0.1
 * @date 2026-07-05
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "led_driver.h"
#include "ui_service.h"
#include "zbus_chan.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(led_driver, LOG_LEVEL_INF);

#define STRIP_NODE         DT_ALIAS(led_strip)
#define NUM_LEDS           DT_PROP(STRIP_NODE, chain_length)
#define UPDATE_INTERVAL_MS 20

/** @brief Tamanho da stack da thread do led_driver (bytes). */
#define LED_DRIVER_STACK_SIZE 2048

/** @brief Prioridade da thread do led_driver. */
#define LED_DRIVER_PRIORITY 7

/** @brief Profundidade da fila do subscriber do canal de UI. */
#define LED_DRIVER_QUEUE_DEPTH 8

/** @brief Cor da fase WORK (vermelho). */
#define LED_COLOR_WORK ((led_color_t){255, 0, 0})

/** @brief Cor da fase SHORT_BREAK (verde). */
#define LED_COLOR_BREAK ((led_color_t){0, 255, 0})

/** @brief Cor da fase LONG_BREAK (azul). */
#define LED_COLOR_LONG_BREAK ((led_color_t){0, 0, 255})

/** @brief Cor do estado pausado (branco). */
#define LED_COLOR_PAUSED ((led_color_t){255, 255, 255})

/** @brief Cor apagada (preto). */
#define LED_COLOR_OFF ((led_color_t){0, 0, 0})

static const struct device *const strip_dev = DEVICE_DT_GET(STRIP_NODE);
static led_channel_t led_channels[NUM_LEDS];
static struct led_rgb strip_pixels[NUM_LEDS];
static struct k_work_delayable led_work;

static led_color_t interpolate_color(led_color_t c1, led_color_t c2, uint32_t step,
				     uint32_t max_step)
{
	led_color_t res;
	if (max_step == 0) {
		return c1;
	}
	res.r = (uint8_t)(c1.r + ((int32_t)(c2.r - c1.r) * (int32_t)step) / (int32_t)max_step);
	res.g = (uint8_t)(c1.g + ((int32_t)(c2.g - c1.g) * (int32_t)step) / (int32_t)max_step);
	res.b = (uint8_t)(c1.b + ((int32_t)(c2.b - c1.b) * (int32_t)step) / (int32_t)max_step);
	return res;
}

static bool colors_equal(led_color_t c1, led_color_t c2)
{
	return (c1.r == c2.r) && (c1.g == c2.g) && (c1.b == c2.b);
}

static bool has_active_effects(void)
{
	for (uint32_t i = 0; i < NUM_LEDS; i++) {
		if (led_channels[i].effect != LED_EFFECT_OFF &&
		    led_channels[i].effect != LED_EFFECT_SOLID) {
			return true;
		}
	}
	return false;
}

static void led_work_handler(struct k_work *work)
{
	bool needs_update = false;

	for (uint32_t i = 0; i < NUM_LEDS; i++) {
		led_channel_t *ch = &led_channels[i];
		if (ch->effect == LED_EFFECT_OFF || ch->effect == LED_EFFECT_SOLID) {
			continue;
		}

		ch->elapsed_ms += UPDATE_INTERVAL_MS;
		if (ch->elapsed_ms >= ch->period_ms) {
			ch->elapsed_ms %= ch->period_ms;
		}

		led_color_t new_color = ch->current_color;

		switch (ch->effect) {
		case LED_EFFECT_BLINK: {
			uint32_t half_period = ch->period_ms / 2;
			if (ch->elapsed_ms < half_period) {
				new_color = ch->color1;
			} else {
				new_color = ch->color2;
			}
			break;
		}
		case LED_EFFECT_FADE: {
			uint32_t half_period = ch->period_ms / 2;
			if (half_period == 0) {
				break;
			}
			if (ch->elapsed_ms <= half_period) {
				new_color = interpolate_color(ch->color1, ch->color2,
							      ch->elapsed_ms, half_period);
			} else {
				uint32_t step = ch->elapsed_ms - half_period;
				new_color = interpolate_color(ch->color2, ch->color1, step,
							      half_period);
			}
			break;
		}
		case LED_EFFECT_STROBE: {
			uint32_t on_time = (ch->period_ms > 50) ? 50 : (ch->period_ms / 4);
			if (ch->elapsed_ms < on_time) {
				new_color = ch->color1;
			} else {
				new_color = (led_color_t){0, 0, 0};
			}
			break;
		}
		default:
			break;
		}

		if (!colors_equal(ch->current_color, new_color)) {
			ch->current_color = new_color;
			strip_pixels[i].r = new_color.r;
			strip_pixels[i].g = new_color.g;
			strip_pixels[i].b = new_color.b;
			needs_update = true;
		}
	}

	if (needs_update) {
		led_strip_update_rgb(strip_dev, strip_pixels, NUM_LEDS);
	}

	if (has_active_effects()) {
		k_work_schedule(&led_work, K_MSEC(UPDATE_INTERVAL_MS));
	}
}

int led_driver_init(void)
{
	if (!device_is_ready(strip_dev)) {
		return -ENODEV;
	}

	k_work_init_delayable(&led_work, led_work_handler);

	for (uint32_t i = 0; i < NUM_LEDS; i++) {
		led_channels[i].effect = LED_EFFECT_OFF;
		led_channels[i].color1 = (led_color_t){0, 0, 0};
		led_channels[i].color2 = (led_color_t){0, 0, 0};
		led_channels[i].current_color = (led_color_t){0, 0, 0};
		led_channels[i].period_ms = 1000;
		led_channels[i].elapsed_ms = 0;

		strip_pixels[i].r = 0;
		strip_pixels[i].g = 0;
		strip_pixels[i].b = 0;
	}

	return led_strip_update_rgb(strip_dev, strip_pixels, NUM_LEDS);
}

void led_driver_set_effect(uint32_t index, led_effect_t effect, led_color_t c1, led_color_t c2,
			   uint32_t period_ms)
{
	if (index >= NUM_LEDS) {
		return;
	}
	led_channel_t *ch = &led_channels[index];
	ch->effect = effect;
	ch->color1 = c1;
	ch->color2 = c2;
	ch->period_ms = (period_ms > 0) ? period_ms : 1;
	ch->elapsed_ms = 0;

	if (effect == LED_EFFECT_OFF) {
		ch->current_color = (led_color_t){0, 0, 0};
	} else if (effect == LED_EFFECT_SOLID) {
		ch->current_color = c1;
	}

	if (effect == LED_EFFECT_OFF || effect == LED_EFFECT_SOLID) {
		strip_pixels[index].r = ch->current_color.r;
		strip_pixels[index].g = ch->current_color.g;
		strip_pixels[index].b = ch->current_color.b;
		led_strip_update_rgb(strip_dev, strip_pixels, NUM_LEDS);
	} else {
		k_work_schedule(&led_work, K_MSEC(UPDATE_INTERVAL_MS));
	}
}

void led_driver_set_color(uint32_t index, led_color_t color)
{
	led_driver_set_effect(index, LED_EFFECT_SOLID, color, (led_color_t){0, 0, 0}, 0);
}

void led_driver_set_all(led_effect_t effect, led_color_t c1, led_color_t c2, uint32_t period_ms)
{
	for (uint32_t i = 0; i < NUM_LEDS; i++) {
		led_channel_t *ch = &led_channels[i];
		ch->effect = effect;
		ch->color1 = c1;
		ch->color2 = c2;
		ch->period_ms = (period_ms > 0) ? period_ms : 1;
		ch->elapsed_ms = 0;

		if (effect == LED_EFFECT_OFF) {
			ch->current_color = (led_color_t){0, 0, 0};
		} else if (effect == LED_EFFECT_SOLID) {
			ch->current_color = c1;
		}

		strip_pixels[i].r = ch->current_color.r;
		strip_pixels[i].g = ch->current_color.g;
		strip_pixels[i].b = ch->current_color.b;
	}

	if (effect == LED_EFFECT_OFF || effect == LED_EFFECT_SOLID) {
		led_strip_update_rgb(strip_dev, strip_pixels, NUM_LEDS);
	} else {
		k_work_schedule(&led_work, K_MSEC(UPDATE_INTERVAL_MS));
	}
}

/**
 * @brief Cor fixa para um estado/fase: branco se pausado, senao a cor da fase.
 *
 * @param state Estado de operacao do timer (rodando ou pausado).
 * @param phase Fase atual do ciclo.
 * @return led_color_t Cor a aplicar no LED.
 */
static led_color_t led_color_for(enum ui_run_state state, enum ui_phase phase);

/**
 * @brief Thread do led_driver: inicializa o strip e segue a fase do Pomodoro
 *        pelo canal de UI, pintando cor solida por fase (branco quando pausado).
 *
 * @param p1 Nao utilizado.
 * @param p2 Nao utilizado.
 * @param p3 Nao utilizado.
 */
static void led_driver_thread(void *p1, void *p2, void *p3);

ZBUS_SUBSCRIBER_DEFINE(led_driver_subscriber, LED_DRIVER_QUEUE_DEPTH);

ZBUS_CHAN_ADD_OBS(ui_cmd_chan, led_driver_subscriber, 4);

K_THREAD_DEFINE(led_driver_tid, LED_DRIVER_STACK_SIZE, led_driver_thread, NULL, NULL, NULL,
		LED_DRIVER_PRIORITY, 0, 0);

static led_color_t led_color_for(enum ui_run_state state, enum ui_phase phase)
{
	led_color_t color;

	if (state == UI_STATE_PAUSED) {
		return LED_COLOR_PAUSED;
	}

	switch (phase) {
	case UI_PHASE_WORK: {
		color = LED_COLOR_WORK;
	} break;
	case UI_PHASE_SHORT_BREAK: {
		color = LED_COLOR_BREAK;
	} break;
	case UI_PHASE_LONG_BREAK: {
		color = LED_COLOR_LONG_BREAK;
	} break;
	default: {
		color = LED_COLOR_OFF;
	} break;
	}

	return color;
}

static void led_driver_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	const struct zbus_channel *chan;
	led_color_t last = {0, 0, 0};

	int rc = led_driver_init();

	if (rc) {
		LOG_ERR("led_driver_init falhou: %d", rc);
		return;
	}

	while (!zbus_sub_wait(&led_driver_subscriber, &chan, K_FOREVER)) {
		struct ui_msg msg = {0};

		if (&ui_cmd_chan != chan) {
			continue;
		}

		rc = zbus_chan_read(&ui_cmd_chan, &msg, K_MSEC(200));
		if (rc) {
			LOG_WRN("zbus_chan_read (ui) falhou: %d", rc);
			continue;
		}

		if (msg.screen != UI_SCREEN_MAIN) {
			continue;
		}

		led_color_t color = led_color_for(msg.data.main.state, msg.data.main.phase);

		if (!colors_equal(color, last)) {
			last = color;
			led_driver_set_all(LED_EFFECT_SOLID, color, LED_COLOR_OFF, 0);
		}
	}
}
