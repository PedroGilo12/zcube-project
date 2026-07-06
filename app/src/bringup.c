#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/drivers/led.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/input/input.h>
#include <zephyr/display/cfb.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <stdio.h>
#include <stdlib.h>

LOG_MODULE_REGISTER(bringup, LOG_LEVEL_INF);

#define STRIP_NODE  DT_ALIAS(led_strip)
#define ACCEL_NODE  DT_ALIAS(accel0)
#define BUZZER_NODE DT_ALIAS(buzzer)

#define STRIP_NUM_PIXELS DT_PROP(STRIP_NODE, chain_length)
#define BEEP_MS          60
#define REFRESH_MS       200
#define BRIGHT           CONFIG_SAMPLE_LED_BRIGHTNESS

static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);
static const struct device *const accel = DEVICE_DT_GET(ACCEL_NODE);
static const struct led_dt_spec buzzer = LED_DT_SPEC_GET(BUZZER_NODE);

static const struct device *const display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

static const struct led_rgb palette[] = {
	{.r = 0, .g = 0, .b = 0},      /* off   */
	{.r = BRIGHT, .g = 0, .b = 0}, /* red   */
	{.r = 0, .g = BRIGHT, .b = 0}, /* green */
	{.r = 0, .g = 0, .b = BRIGHT}, /* blue  */
};

static struct led_rgb pixels[STRIP_NUM_PIXELS];
static K_SEM_DEFINE(press_sem, 0, 10);

/* Callback dos botões, quando ocorre interrupção no subsistema input verifica se a interrupção
gerada é a interrupção de um dos botões e incrementa o semaforo. */
static void button_cb(struct input_event *evt, void *user_data)
{
	/* so reage ao pressionar (value==1) */
	if (evt->type != INPUT_EV_KEY || evt->value == 0) {
		return;
	}

	k_sem_give(&press_sem);
}
INPUT_CALLBACK_DEFINE(NULL, button_cb, NULL);

static void show_color(size_t idx)
{
	for (size_t i = 0; i < STRIP_NUM_PIXELS; i++) {
		pixels[i] = palette[idx];
	}

	int rc = led_strip_update_rgb(strip, pixels, STRIP_NUM_PIXELS);

	if (rc) {
		LOG_ERR("led_strip_update_rgb: %d", rc);
	}
}

static void beep(void)
{
	led_on_dt(&buzzer);
	k_msleep(BEEP_MS);
	led_off_dt(&buzzer);
}

/* Formata "eixo: valor" com 2 casas decimais e sinal, sem depender do float. */
static void fmt_axis(char *buf, size_t n, char axis, const struct sensor_value *v)
{
	int32_t whole = v->val1;
	int32_t frac = v->val2 / 10000;
	bool neg = (v->val1 < 0) || (v->val1 == 0 && v->val2 < 0);

	if (whole < 0) {
		whole = -whole;
	}
	if (frac < 0) {
		frac = -frac;
	}

	snprintf(buf, n, "%c: %s%d.%02d", axis, neg ? "-" : "", whole, frac);
}

/* Inicializa o OLED via CFB. */
static bool display_init(void)
{
	if (!device_is_ready(display)) {
		LOG_ERR("OLED not ready");
		return false;
	}

	if (display_set_pixel_format(display, PIXEL_FORMAT_MONO10) != 0 &&
	    display_set_pixel_format(display, PIXEL_FORMAT_MONO01) != 0) {
		LOG_ERR("OLED: pixel format not support");
		return false;
	}

	if (cfb_framebuffer_init(display) != 0) {
		LOG_ERR("OLED: cfb init fail");
		return false;
	}

	cfb_framebuffer_clear(display, true);
	display_blanking_off(display);
	cfb_framebuffer_set_font(display, 0);
	return true;
}

static void display_accel(const struct sensor_value a[3], uint8_t line_h)
{
	char line[20];

	cfb_framebuffer_clear(display, false);
	fmt_axis(line, sizeof(line), 'x', &a[0]);
	cfb_print(display, line, 0, 0);
	fmt_axis(line, sizeof(line), 'y', &a[1]);
	cfb_print(display, line, 0, line_h);
	fmt_axis(line, sizeof(line), 'z', &a[2]);
	cfb_print(display, line, 0, 2 * line_h);
	cfb_framebuffer_finalize(display);
}

int main(void)
{
	size_t color = 0;
	bool have_mpu, have_display;
	uint8_t fw = 0, fh = 10;

	if (!device_is_ready(strip)) {
		LOG_ERR("WS2812 not ready");
		return 0;
	}

	if (!led_is_ready_dt(&buzzer)) {
		LOG_ERR("Buzzer not ready");
		return 0;
	}

	have_mpu = device_is_ready(accel);
	if (!have_mpu) {
		LOG_ERR("MPU6050 not ready");
	}

	have_display = display_init();
	if (have_display) {
		cfb_get_font_size(display, 0, &fw, &fh);
	}

	LOG_INF("Bringup running...");
	show_color(color);

	while (1) {
		/* Acorda imediatamente num aperto, ou a cada REFRESH_MS
		 * para atualizar a tela.
		 */
		if (k_sem_take(&press_sem, K_MSEC(REFRESH_MS)) == 0) {
			beep();
			color = (color + 1) % ARRAY_SIZE(palette);
			show_color(color);
			LOG_INF("Led color -> index %u", color);
		}

		if (have_mpu) {
			struct sensor_value a[3];
			int rc = sensor_sample_fetch(accel);

			if (rc == 0) {
				rc = sensor_channel_get(accel, SENSOR_CHAN_ACCEL_XYZ, a);
			}

			if (rc == 0) {
				// LOG_INF("accel x=%d.%06d y=%d.%06d z=%d.%06d",
				// 	a[0].val1, abs(a[0].val2), a[1].val1, abs(a[1].val2),
				// 	a[2].val1, abs(a[2].val2));
				if (have_display) {
					display_accel(a, fh);
				}
			} else {
				LOG_ERR("MPU fetch/get: %d", rc);
			}
		}
	}

	return 0;
}
