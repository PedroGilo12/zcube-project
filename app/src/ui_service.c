
/**
 * @file ui_service.c
 * @author your name (you@domain.com)
 * @brief Servico de UI: renderiza a tela do timer Pomodoro em display OLED e
 *        trata a entrada de botoes.
 * @version 0.1
 * @date 2026-07-05
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "ui_service.h"
#include "app_fsm.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/display/cfb.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(ui_service, LOG_LEVEL_DBG);

/** @brief Tamanho da stack da thread de UI (bytes). */
#define UI_SERVICE_STACK_SIZE 4096

/** @brief Prioridade da thread de UI. */
#define UI_SERVICE_PRIORITY 6

/** @brief Node do display escolhido no devicetree. */
#define UI_SERVICE_DISPLAY_NODE DT_CHOSEN(zephyr_display)

/** @brief Largura da tela em pixels. */
#define UI_SERVICE_SCREEN_WIDHT DT_PROP(UI_SERVICE_DISPLAY_NODE, width)

/** @brief Centro vertical dos dots de ciclo na barra de status. */
#define UI_SERVICE_DOTS_Y 6

/** @brief Lado de cada dot de ciclo (px). */
#define UI_SERVICE_DOT_SZ 4

/** @brief Espaco entre dots de ciclo (px). */
#define UI_SERVICE_DOT_GAP 3

/** @brief Indice da fonte nativa do tempo grande (20x32). */
#define UI_SERVICE_TIME_FONT_IDX 2

/** @brief Coordenada Y do topo do MM:SS. */
#define UI_SERVICE_TIME_Y 16

/** @brief Coordenada Y do selo de fase/acao no rodape. */
#define UI_SERVICE_PILL_Y 48

/** @brief Altura da seta de acao (px). */
#define UI_SERVICE_ARROW_H 6

/** @brief Metade da largura da base da seta de acao (px). */
#define UI_SERVICE_ARROW_HALF_W 6

/** @brief Centro X da seta de acao (px). */
#define UI_SERVICE_ARROW_CX 110

/** @brief Altura da linha. */
#define UI_SERVICE_LINE_HEIGHT 16

/** @brief Largura da fonte 0. */
#define UI_SERVICE_FONT_WIDTH 10

/** @brief Largura do tempo. */
#define UI_SERVICE_TIME_WIDTH 20

/** @brief Altura do tempo na tela. */
#define UI_SERVICE_TIME_HEIGHT 32

/**
 * @brief Associa um id de tela a sua funcao de renderizacao.
 *
 */
struct ui_screen {
	enum ui_screen_id id;
	void (*render)(const struct device *disp, const struct ui_msg *msg);
};

/**
 * @brief Preenche (acende) um retangulo na tela.
 *
 * @param disp Dispositivo de display.
 * @param x Coordenada X do canto superior esquerdo.
 * @param y Coordenada Y do canto superior esquerdo.
 * @param w Largura do retangulo (px).
 * @param h Altura do retangulo (px).
 */
static void ui_fill_rect(const struct device *disp, int x, int y, int w, int h);

/**
 * @brief Desenha o MM:SS grande centralizado.
 *
 * @param disp Dispositivo de display.
 * @param mm Minutos a exibir.
 * @param ss Segundos a exibir.
 */
static void ui_draw_big_time(const struct device *disp, uint8_t mm, uint8_t ss);

/**
 * @brief Desenha a barra de status com o nome do perfil ativo.
 *
 * @param disp Dispositivo de display.
 * @param msg [in] Mensagem com os dados da tela.
 */
static void ui_draw_status_bar(const struct device *disp, const struct ui_msg *msg);

/**
 * @brief Desenha o selo de fase/acao no rodape.
 *
 * @param disp Dispositivo de display.
 * @param state Estado de operacao do timer.
 * @param phase Fase atual do ciclo.
 */
static void ui_draw_state_pill(const struct device *disp, enum ui_run_state state,
			       enum ui_phase phase);

/**
 * @brief Desenha os dots de ciclo (preenchidos = concluidos).
 *
 * @param disp Dispositivo de display.
 * @param done Quantidade de pomodoros concluidos.
 * @param total Total de pomodoros do conjunto.
 */
static void draw_cycle_dots(const struct device *disp, uint8_t done, uint8_t total);

/**
 * @brief Desenha a seta de acao inferior na direcao indicada.
 *
 * @param disp Dispositivo de display.
 * @param dir Direcao da seta (cima ou baixo).
 */
static void draw_action_arrow(const struct device *disp, enum ui_arrow_dir dir);

/**
 * @brief Desenha as setas < > de troca de perfil nas laterais.
 *
 * @param disp Dispositivo de display.
 */
static void draw_profile_arrows(const struct device *disp);

/**
 * @brief Renderiza a tela principal do timer.
 *
 * @param disp Dispositivo de display.
 * @param msg [in] Mensagem com os dados da tela.
 */
static void ui_screen_timer_render(const struct device *disp, const struct ui_msg *msg);

/**
 * @brief Despacha a renderizacao para a tela indicada na mensagem.
 *
 * @param disp Dispositivo de display.
 * @param msg [in] Mensagem com o id da tela e seus dados.
 */
static void ui_render(const struct device *disp, const struct ui_msg *msg);

/**
 * @brief Inicializa o display OLED e o framebuffer CFB.
 *
 * @return true em caso de sucesso, false em caso de falha.
 */
static bool ui_display_init(void);

/**
 * @brief Thread de UI: inicializa o display e processa mensagens do zbus.
 *
 * @param p1 Nao utilizado.
 * @param p2 Nao utilizado.
 * @param p3 Nao utilizado.
 */
static void ui_service_thread(void *p1, void *p2, void *p3);

/**
 * @brief Callback de input: traduz teclas em eventos da FSM.
 *
 * @param evt [in] Evento de input recebido.
 * @param user_data Nao utilizado.
 */
static void ui_input_cb(struct input_event *evt, void *user_data);

/**
 * @brief Ponteiro para o dispositivo de display OLED.
 *
 */
static const struct device *const display = DEVICE_DT_GET(UI_SERVICE_DISPLAY_NODE);

/**
 * @brief Singleton do serviço de interface de usuario.
 *
 */
static struct ui_service {
	const struct ui_screen
		ui_screens[_UI_SCREEN_AMOUNT]; /**< Tabela de telas registradas (id -> render)*/
} self = {
	.ui_screens =
		{
			[UI_SCREEN_MAIN] =
				{
					.id = UI_SCREEN_MAIN,
					.render = ui_screen_timer_render,
				},
		},
};

INPUT_CALLBACK_DEFINE(NULL, ui_input_cb, NULL);

ZBUS_SUBSCRIBER_DEFINE(ui_service_subscriber, 8);

ZBUS_CHAN_DEFINE(ui_cmd_chan, struct ui_msg, NULL, NULL, ZBUS_OBSERVERS(ui_service_subscriber),
		 ZBUS_MSG_INIT(0));

K_THREAD_DEFINE(ui_service_tid, UI_SERVICE_STACK_SIZE, ui_service_thread, NULL, NULL, NULL,
		UI_SERVICE_PRIORITY, 0, 0);

/**
 * @brief Publica uma mensagem de atualizacao no canal zbus da UI.
 *
 * @param msg [in] Mensagem com o estado a renderizar.
 * @return int 0 em caso de sucesso ou inteiro negativo em caso de falha.
 */
int ui_service_update(const struct ui_msg *msg)
{
	return zbus_chan_pub(&ui_cmd_chan, msg, K_MSEC(50));
}

static void ui_fill_rect(const struct device *disp, int x, int y, int w, int h)
{
	if (w <= 0 || h <= 0) {
		return;
	}
	cfb_invert_area(disp, (uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h);
}

static void ui_draw_big_time(const struct device *disp, uint8_t mm, uint8_t ss)
{
	char buf[6];
	snprintf(buf, sizeof(buf), "%02u:%02u", mm, ss);

	int w = (int)strlen(buf) * UI_SERVICE_TIME_WIDTH;
	int x = 0;
	if (w < UI_SERVICE_SCREEN_WIDHT) {
		x = (UI_SERVICE_SCREEN_WIDHT - w) / 2;
	}

	cfb_framebuffer_set_font(disp, UI_SERVICE_TIME_FONT_IDX);
	cfb_print(disp, buf, (uint16_t)x, UI_SERVICE_TIME_Y);
	cfb_framebuffer_set_font(disp, 0);
}

static void ui_draw_status_bar(const struct device *disp, const struct ui_msg *msg)
{
	const char *name = msg->data.main.profile_name;
	if (name[0] == '\0') {
		name = "--";
	}
	cfb_print(disp, (char *)name, 0, 0);
}

static void ui_draw_state_pill(const struct device *disp, enum ui_run_state state,
			       enum ui_phase phase)
{
	const char *label;
	if (state == UI_STATE_RUNNING) {
		label = "pause";
	} else if (phase == UI_PHASE_WORK) {
		label = "Work";
	} else if (phase == UI_PHASE_SHORT_BREAK) {
		label = "Break";
	} else {
		label = "Long break";
	}
	int region = UI_SERVICE_ARROW_CX - UI_SERVICE_ARROW_HALF_W - 2;
	int w = (int)strlen(label) * UI_SERVICE_FONT_WIDTH;
	int x = 0;
	if (w < region) {
		x = (region - w) / 2;
	}

	cfb_print(disp, (char *)label, (uint16_t)x, UI_SERVICE_PILL_Y);
}

static void draw_cycle_dots(const struct device *disp, uint8_t done, uint8_t total)
{
	if (total == 0) {
		return;
	}
	if (total > 8) {
		total = 8;
	}
	int span = total * UI_SERVICE_DOT_SZ + (total - 1) * UI_SERVICE_DOT_GAP;
	int x = UI_SERVICE_SCREEN_WIDHT - span; /* alinhado a direita */
	if (x < 0) {
		x = 0;
	}

	for (uint8_t i = 0; i < total; i++) {
		if (i < done) {
			ui_fill_rect(disp, x, UI_SERVICE_DOTS_Y, UI_SERVICE_DOT_SZ,
				     UI_SERVICE_DOT_SZ);
		} else {
			struct cfb_position a = {(uint16_t)x, UI_SERVICE_DOTS_Y};
			struct cfb_position b = {(uint16_t)(x + UI_SERVICE_DOT_SZ - 1),
						 UI_SERVICE_DOTS_Y + UI_SERVICE_DOT_SZ - 1};
			cfb_draw_rect(disp, &a, &b);
		}
		x += UI_SERVICE_DOT_SZ + UI_SERVICE_DOT_GAP;
	}
}

static void draw_action_arrow(const struct device *disp, enum ui_arrow_dir dir)
{
	int cx = UI_SERVICE_ARROW_CX;
	int top = UI_SERVICE_PILL_Y +
		  (UI_SERVICE_LINE_HEIGHT - UI_SERVICE_ARROW_H) / 2;

	for (int row = 0; row < UI_SERVICE_ARROW_H; row++) {
		int num = UI_SERVICE_ARROW_H - 1 - row;
		if (dir == UI_ARROW_TOP) {
			num = row;
		}
		int half = UI_SERVICE_ARROW_HALF_W * num / (UI_SERVICE_ARROW_H - 1);
		ui_fill_rect(disp, cx - half, top + row, 2 * half + 1, 1);
	}
}

static void draw_profile_arrows(const struct device *disp)
{
	uint16_t cy = UI_SERVICE_TIME_Y + UI_SERVICE_TIME_HEIGHT / 2;

	cfb_print(disp, (char *)"<", 0, cy - 4);
	cfb_print(disp, (char *)">", UI_SERVICE_SCREEN_WIDHT - 10, cy - 4);
}

static void ui_screen_timer_render(const struct device *disp, const struct ui_msg *msg)
{
	uint32_t rem = msg->data.main.remaining_s;
	uint8_t mm = (uint8_t)MIN(rem / 60, 99);
	uint8_t ss = (uint8_t)(rem % 60);

	cfb_framebuffer_clear(disp, false);

	ui_draw_status_bar(disp, msg);
	ui_draw_big_time(disp, mm, ss);                                               
	draw_cycle_dots(disp, msg->data.main.cycle_done, msg->data.main.cycle_total); 
	ui_draw_state_pill(disp, msg->data.main.state, msg->data.main.phase);         
	draw_action_arrow(disp, msg->data.main.arrow_dir);                            

	if (msg->data.main.state == UI_STATE_PAUSED) {
		draw_profile_arrows(disp);
	}

	cfb_framebuffer_finalize(disp);
}

static void ui_render(const struct device *disp, const struct ui_msg *msg)
{

	if (msg->screen >= ARRAY_SIZE(self.ui_screens)) {
		LOG_WRN("nenhuma tela registrada p/ id %d", msg->screen);
		return;
	}

	self.ui_screens[msg->screen].render(disp, msg);
}

static bool ui_display_init(void)
{
	if (!device_is_ready(display)) {
		LOG_ERR("OLED not ready");
		return false;
	}

	if (display_set_pixel_format(display, PIXEL_FORMAT_MONO10) != 0 &&
	    display_set_pixel_format(display, PIXEL_FORMAT_MONO01) != 0) {
		LOG_ERR("OLED: pixel format not suported");
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

static void ui_service_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	const struct zbus_channel *chan;
	struct ui_msg msg;

	(void)ui_display_init();
	ui_render(display, &(const struct ui_msg){
				   .screen = UI_SCREEN_MAIN,
				   .data.main =
					   {
						   .remaining_s = 1500, /* 25:00 */
						   .state = UI_STATE_PAUSED,
						   .phase = UI_PHASE_WORK,
						   .profile_name = "Classico",
						   .cycle_done = 1,
						   .cycle_total = 4,
					   },
			   });

	while (!zbus_sub_wait(&ui_service_subscriber, &chan, K_FOREVER)) {
		if (chan == &ui_cmd_chan) {
			int rc = zbus_chan_read(&ui_cmd_chan, &msg, K_MSEC(200));
			if (rc) {
				LOG_WRN("zbus_chan_read (ui_cmd) falhou: %d", rc);
				continue;
			}
			ui_render(display, &msg);
		}
	}
}

static void ui_input_cb(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);

	if (evt->type != INPUT_EV_KEY || evt->value == 0) {
		return;
	}

	struct app_fsm_evt_data msg = {0};

	switch (evt->code) {
	case INPUT_KEY_0: {
		msg.id = APP_FSM_EVENT_NEXT;
	} break;
	case INPUT_KEY_1: {
		msg.id = APP_FSM_EVENT_PREVIOUS;
	} break;
	default: {
		/* Do nothing */
	} break;
	}
	LOG_INF("id: %d", msg.id);

	int rc = zbus_chan_pub(&app_fsm_evt_chan, &msg, K_NO_WAIT);

	if (rc) {
		LOG_WRN("fail to publish to button: %d", rc);
	}
}
