/**
 * @file app_fsm.c
 * @author your name (you@domain.com)
 * @brief FSM do Pomodoro baseada em SMF: gerencia as fases WORK/BREAK/LONG_BREAK,
 *        o tick de 1 Hz e publica as atualizacoes de tela no servico de UI.
 * @version 0.1
 * @date 2026-07-05
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "app_fsm.h"
#include "ui_service.h"
#include "orientation_service.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/smf.h>
#include <zephyr/logging/log.h>
#include <stdint.h>


LOG_MODULE_REGISTER(app_fsm_module, LOG_LEVEL_DBG);

/** @brief Tamanho da stack da thread da FSM (bytes). */
#define APP_FSM_STACK_SIZE 4096

/** @brief Prioridade da thread da FSM. */
#define APP_FSM_PRIORITY 5

/** @brief Duracao default do contador de WORK (s). */
#define WORK_DURATION_S (1 * 60)

/** @brief Duracao default do contador de BREAK (s). */
#define BREAK_DURATION_S (1 * 60)

/** @brief Duracao default do contador de LONG_BREAK (s). */
#define LONG_BREAK_DURATION_S (1 * 60)

/** @brief Periodo do tick da FSM (s). */
#define TICK_PERIOD_S 1

/** @brief Quantos WORK concluidos antes de um LONG_BREAK. */
#define WORK_CYCLES_PER_LONG_BREAK 4

/**
 * @brief Estado da FSM do Pomodoro.
 *
 */
enum app_fsm_state {
	APP_FSM_STATE_RUNNING,    /**< Pai: tick/decremento comum. */
	APP_FSM_STATE_WORK,       /**< Filho de RUNNING: fase de trabalho. */
	APP_FSM_STATE_BREAK,      /**< Filho de RUNNING: pausa curta. */
	APP_FSM_STATE_LONG_BREAK, /**< Filho de RUNNING: pausa longa. */
	APP_FSM_STATE_PAUSED,     /**< Irmao: contador congelado. */
	APP_FSM_STATE_CONFIG,     /**< Irmao: configuracao. */
};

/**
 * @brief Contexto e estado da FSM do Pomodoro.
 *
 */
struct app_fsm_obj {
	struct smf_ctx ctx;                   /**< Contexto do framework SMF. */
	struct app_fsm_evt_data evt;          /**< Evento em processamento. */
	uint32_t remaining_s;                 /**< Contador do estado ativo (decrementado pelo tick). */
	uint32_t work_s;                      /**< Valor de contador do WORK. */
	uint32_t break_s;                     /**< Valor de contador do BREAK. */
	uint32_t long_break_s;                /**< Valor de contador do LONG_BREAK. */
	enum ui_phase phase;                  /**< Fase atual: UI e decisao da proxima fase. */
	uint8_t cycle_done;                   /**< WORK concluidos no conjunto. */
	uint8_t cycle_total;                  /**< WORK por conjunto antes do LONG_BREAK. */
	enum ui_arrow_dir arrow_dir;          /**< Direcao da seta de acao (segue a orientacao). */
	const struct smf_state *resume_state; /**< Estado a retomar ao sair de PAUSED. */
};

/**
 * @brief Entry de RUNNING: inicia o timer periodico de tick.
 *
 * @param o [in,out] Contexto da FSM.
 */
static void running_entry(void *o);

/**
 * @brief Run de RUNNING: trata TICK (decremento/avanco) e giro (pausa).
 *
 * @param o [in,out] Contexto da FSM.
 * @return enum smf_state_result Sempre SMF_EVENT_HANDLED.
 */
static enum smf_state_result running_run(void *o);

/**
 * @brief Exit de RUNNING: para o timer periodico de tick.
 *
 * @param o [in,out] Contexto da FSM.
 */
static void running_exit(void *o);

/**
 * @brief Entry de WORK: fixa a fase de trabalho e atualiza a UI.
 *
 * @param o [in,out] Contexto da FSM.
 */
static void work_entry(void *o);

/**
 * @brief Entry de BREAK: fixa a pausa curta e atualiza a UI.
 *
 * @param o [in,out] Contexto da FSM.
 */
static void break_entry(void *o);

/**
 * @brief Entry de LONG_BREAK: fixa a pausa longa e atualiza a UI.
 *
 * @param o [in,out] Contexto da FSM.
 */
static void long_break_entry(void *o);

/**
 * @brief Entry de PAUSED: mostra a fase atual congelada na UI.
 *
 * @param o [in,out] Contexto da FSM.
 */
static void paused_entry(void *o);

/**
 * @brief Run de PAUSED: retoma o estado ao girar para FRONT/TOP.
 *
 * @param o [in,out] Contexto da FSM.
 * @return enum smf_state_result Sempre SMF_EVENT_HANDLED.
 */
static enum smf_state_result paused_run(void *o);

/**
 * @brief Fim de fase: escolhe a proxima, carrega sua duracao e vai para PAUSED.
 *
 * @param s [in,out] Contexto da FSM.
 */
static void advance_phase(struct app_fsm_obj *s);

/**
 * @brief Reflete o estado atual (contador, run/paused e seta) no display.
 *
 * @param s [in] Contexto da FSM com os dados a exibir.
 * @param state Estado de operacao (rodando ou pausado) para a UI.
 */
static void publish_ui(const struct app_fsm_obj *s, enum ui_run_state state);

/**
 * @brief Callback do timer: publica APP_FSM_EVENT_TICK na fila da FSM.
 *
 * @param t Timer que disparou (nao utilizado).
 */
static void app_fsm_tick_cb(struct k_timer *t);

/**
 * @brief Thread da FSM: inicializa o estado e processa eventos do zbus.
 *
 * @param p1 Nao utilizado.
 * @param p2 Nao utilizado.
 * @param p3 Nao utilizado.
 */
static void app_fsm_thread(void *p1, void *p2, void *p3);

/**
 * @brief Singleton da FSM do Pomodoro.
 *
 */
static struct app_fsm_obj self;

/**
 * @brief Tabela de estados da FSM (id -> handlers e hierarquia).
 *
 */
static const struct smf_state app_states[] = {
	[APP_FSM_STATE_RUNNING] = SMF_CREATE_STATE(running_entry, running_run, running_exit,
						   NULL, &app_states[APP_FSM_STATE_WORK]),
	[APP_FSM_STATE_WORK] = SMF_CREATE_STATE(work_entry, NULL, NULL,
						&app_states[APP_FSM_STATE_RUNNING], NULL),
	[APP_FSM_STATE_BREAK] = SMF_CREATE_STATE(break_entry, NULL, NULL,
						 &app_states[APP_FSM_STATE_RUNNING], NULL),
	[APP_FSM_STATE_LONG_BREAK] = SMF_CREATE_STATE(long_break_entry, NULL, NULL,
						      &app_states[APP_FSM_STATE_RUNNING], NULL),
	[APP_FSM_STATE_PAUSED] = SMF_CREATE_STATE(paused_entry, paused_run, NULL,
						  NULL, NULL),
	[APP_FSM_STATE_CONFIG] = SMF_CREATE_STATE(NULL, NULL, NULL,
						  NULL, NULL),
};

K_TIMER_DEFINE(app_fsm_tick, app_fsm_tick_cb, NULL);

ZBUS_SUBSCRIBER_DEFINE(app_fsm_subscriber, 16);

ZBUS_CHAN_DEFINE(app_fsm_evt_chan,
		 struct app_fsm_evt_data,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS(app_fsm_subscriber),
		 ZBUS_MSG_INIT(0)
);

K_THREAD_DEFINE(app_fsm_tid, APP_FSM_STACK_SIZE,
		app_fsm_thread, NULL, NULL, NULL,
		APP_FSM_PRIORITY, 0, 0);

static void app_fsm_tick_cb(struct k_timer *t)
{
	ARG_UNUSED(t);

	struct app_fsm_evt_data msg = { .id = APP_FSM_EVENT_TICK };
	(void)zbus_chan_pub(&app_fsm_evt_chan, &msg, K_NO_WAIT); /* K_NO_WAIT: seguro em ISR */
}

static void publish_ui(const struct app_fsm_obj *s, enum ui_run_state state)
{
	struct ui_msg msg = {
		.screen = UI_SCREEN_MAIN,
		.data.main = {
			.remaining_s = s->remaining_s,
			.state = state,
			.phase = s->phase,
			.profile_name = "Teste",
			.cycle_done = s->cycle_done,
			.cycle_total = s->cycle_total,
			.arrow_dir = s->arrow_dir,
		},
	};
	(void)ui_service_update(&msg);
}

static void running_entry(void *o)
{
	ARG_UNUSED(o);
	k_timer_start(&app_fsm_tick, K_SECONDS(TICK_PERIOD_S), K_SECONDS(TICK_PERIOD_S));
}

static void advance_phase(struct app_fsm_obj *s)
{
	switch (s->phase) {
	case UI_PHASE_WORK: {
		s->cycle_done++;
		if (s->cycle_done >= s->cycle_total) {
			s->phase = UI_PHASE_LONG_BREAK;
			s->remaining_s = s->long_break_s;
			s->resume_state = &app_states[APP_FSM_STATE_LONG_BREAK];
		} else {
			s->phase = UI_PHASE_SHORT_BREAK;
			s->remaining_s = s->break_s;
			s->resume_state = &app_states[APP_FSM_STATE_BREAK];
		}
	} break;
	case UI_PHASE_SHORT_BREAK: {
		s->phase = UI_PHASE_WORK;
		s->remaining_s = s->work_s;
		s->resume_state = &app_states[APP_FSM_STATE_WORK];
	} break;
	case UI_PHASE_LONG_BREAK: {
		s->cycle_done = 0;
		s->phase = UI_PHASE_WORK;
		s->remaining_s = s->work_s;
		s->resume_state = &app_states[APP_FSM_STATE_WORK];
	} break;
	default: {
		/* Do nothing */
	} break;
	}

	smf_set_state(SMF_CTX(&self), &app_states[APP_FSM_STATE_PAUSED]);
}

static enum smf_state_result running_run(void *o)
{
	struct app_fsm_obj *s = (struct app_fsm_obj *)o;

	switch (s->evt.id) {
	case APP_FSM_EVENT_TICK: {
		if (s->remaining_s > 0) {
			s->remaining_s--;
		}
		if (s->remaining_s == 0) {
			advance_phase(s);
			break;
		}
		publish_ui(s, UI_STATE_RUNNING);
	} break;
	case APP_FSM_EVENT_ORIENTATION_CHANGED: {
		if (s->evt.data.orientation.current_face == ORIENTATION_FRONT ||
		    s->evt.data.orientation.current_face == ORIENTATION_TOP) {
			smf_set_state(SMF_CTX(&self), &app_states[APP_FSM_STATE_PAUSED]);
		}
	} break;
	default: {
		/* Do nothing */
	} break;
	}

	return SMF_EVENT_HANDLED;
}

static void running_exit(void *o)
{
	ARG_UNUSED(o);
	k_timer_stop(&app_fsm_tick);
}

static void work_entry(void *o)
{
	struct app_fsm_obj *s = (struct app_fsm_obj *)o;

	s->phase = UI_PHASE_WORK;
	s->resume_state = &app_states[APP_FSM_STATE_WORK];

	publish_ui(s, UI_STATE_RUNNING);
}

static void break_entry(void *o)
{
	struct app_fsm_obj *s = (struct app_fsm_obj *)o;

	s->phase = UI_PHASE_SHORT_BREAK;
	s->resume_state = &app_states[APP_FSM_STATE_BREAK];

	publish_ui(s, UI_STATE_RUNNING);
}

static void long_break_entry(void *o)
{
	struct app_fsm_obj *s = (struct app_fsm_obj *)o;

	s->phase = UI_PHASE_LONG_BREAK;
	s->resume_state = &app_states[APP_FSM_STATE_LONG_BREAK];

	publish_ui(s, UI_STATE_RUNNING);
}

static void paused_entry(void *o)
{
	struct app_fsm_obj *s = (struct app_fsm_obj *)o;

	publish_ui(s, UI_STATE_PAUSED);
}

static enum smf_state_result paused_run(void *o)
{
	struct app_fsm_obj *s = (struct app_fsm_obj *)o;

	if (s->evt.id == APP_FSM_EVENT_ORIENTATION_CHANGED &&
	    (s->evt.data.orientation.current_face == ORIENTATION_FRONT ||
	     s->evt.data.orientation.current_face == ORIENTATION_TOP)) {
		smf_set_state(SMF_CTX(&self), s->resume_state);
	}

	return SMF_EVENT_HANDLED;
}

static void app_fsm_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	const struct zbus_channel *chan;

	self.work_s = WORK_DURATION_S;
	self.break_s = BREAK_DURATION_S;
	self.long_break_s = LONG_BREAK_DURATION_S;
	self.remaining_s = self.work_s;
	self.phase = UI_PHASE_WORK;
	self.cycle_done = 0;
	self.cycle_total = WORK_CYCLES_PER_LONG_BREAK;
	self.resume_state = &app_states[APP_FSM_STATE_WORK];
	self.arrow_dir = UI_ARROW_TOP; 

	/* Comeca parado mostrando WORK; girar para FRONT/TOP inicia a contagem. */
	smf_set_initial(SMF_CTX(&self), &app_states[APP_FSM_STATE_PAUSED]);

	while (!zbus_sub_wait(&app_fsm_subscriber, &chan, K_FOREVER)) {
		struct app_fsm_evt_data evt = {0};

		if (&app_fsm_evt_chan == chan) {
			int rc = zbus_chan_read(&app_fsm_evt_chan, &evt, K_MSEC(200));
			if (rc) {
				LOG_WRN("zbus_chan_read falhou: %d", rc);
				continue;
			}

			/* A seta e derivada da face atual, atualizada ANTES de rodar a
			 * FSM para que qualquer publish_ui use o valor correto. */
			if (evt.id == APP_FSM_EVENT_ORIENTATION_CHANGED) {
				switch (evt.data.orientation.current_face) {
				case ORIENTATION_FRONT: {
					self.arrow_dir = UI_ARROW_TOP;
				} break;
				case ORIENTATION_TOP: {
					self.arrow_dir = UI_ARROW_BOTTOM;
				} break;
				default: {
					/* outras faces nao alteram a seta */
				} break;
				}
			}

			self.evt = evt;
			smf_run_state(SMF_CTX(&self));
		}
	}
}
