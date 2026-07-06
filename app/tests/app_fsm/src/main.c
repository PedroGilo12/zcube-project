#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

#include "ui_service.h"
#include "app_fsm.h"
#include "orientation_service.h"
#include "zbus_chan.h"

#define EXPECTED_CYCLE_TOTAL 4

static struct ui_msg last_ui;
static struct ui_buzzer_msg last_buzzer;
static K_SEM_DEFINE(ui_sem, 0, 10);
static K_SEM_DEFINE(buzzer_sem, 0, 10);

int ui_service_update(const struct ui_msg *msg)
{
	last_ui = *msg;
	k_sem_give(&ui_sem);
	return 0;
}

int ui_service_buzzer(const struct ui_buzzer_msg *msg)
{
	last_buzzer = *msg;
	k_sem_give(&buzzer_sem);
	return 0;
}

/**
 * @brief Publica um evento na FSM e espera o proximo update de UI.
 *
 * @param e Evento a publicar.
 * @return struct ui_msg Ultimo snapshot recebido da FSM.
 */
static struct ui_msg pub_wait(struct app_fsm_evt_data *e)
{
	zassert_ok(zbus_chan_pub(&app_fsm_evt_chan, e, K_MSEC(50)), "falha ao publicar evento");
	zassert_ok(k_sem_take(&ui_sem, K_MSEC(500)), "FSM nao respondeu com update de UI");
	return last_ui;
}

/** @brief Retoma de PAUSED para RUNNING girando para a face FRONT. */
static struct ui_msg resume(void)
{
	struct app_fsm_evt_data e = {
		.id = APP_FSM_EVENT_ORIENTATION_CHANGED,
		.data.orientation.current_face = ORIENTATION_FRONT,
	};
	return pub_wait(&e);
}

/** @brief Envia um TICK (decremento de 1 s). */
static struct ui_msg tick(void)
{
	struct app_fsm_evt_data e = {.id = APP_FSM_EVENT_TICK};
	return pub_wait(&e);
}

/** @brief Reseta a FSM via CONFIG para WORK/PAUSED com as duracoes dadas. */
static struct ui_msg reset_config(uint16_t work_s, uint16_t break_s, uint16_t long_break_s)
{
	struct app_fsm_evt_data e = {
		.id = APP_FSM_EVENT_CONFIG,
		.data.config = {
			.work_s = work_s,
			.break_s = break_s,
			.long_break_s = long_break_s,
		},
	};
	return pub_wait(&e);
}

/**
 * @brief Completa uma fase RUNNING de 1 s: retoma (FRONT) e envia 1 TICK.
 *
 * @return struct ui_msg Snapshot apos o avanco de fase (estado PAUSED).
 */
static struct ui_msg complete_1s_phase(void)
{
	resume();
	return tick();
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);

	k_sem_reset(&ui_sem);
	k_sem_reset(&buzzer_sem);

	reset_config(1, 1, 1);
}

ZTEST_SUITE(app_fsm, NULL, NULL, before, NULL, NULL);

/* ---- Testes ---------------------------------------------------------- */

ZTEST(app_fsm, test_reset_via_config)
{
	zassert_equal(last_ui.data.main.state, UI_STATE_PAUSED, "deveria iniciar pausado");
	zassert_equal(last_ui.data.main.phase, UI_PHASE_WORK, "fase inicial deve ser WORK");
	zassert_equal(last_ui.data.main.cycle_done, 0, "cycle_done deve zerar");
	zassert_equal(last_ui.data.main.cycle_total, EXPECTED_CYCLE_TOTAL, "cycle_total incorreto");
	zassert_equal(last_ui.data.main.remaining_s, 1, "remaining_s deve ser work_s");
}

ZTEST(app_fsm, test_resume_from_paused_runs_work)
{
	reset_config(5, 5, 5);

	struct ui_msg ui = resume();

	zassert_equal(ui.data.main.state, UI_STATE_RUNNING, "deveria estar rodando");
	zassert_equal(ui.data.main.phase, UI_PHASE_WORK, "fase deve ser WORK");
	zassert_equal(ui.data.main.remaining_s, 5, "remaining_s deve ser work_s");
}

ZTEST(app_fsm, test_tick_decrements_remaining)
{
	reset_config(5, 5, 5);
	resume();

	struct ui_msg ui = tick();

	zassert_equal(ui.data.main.state, UI_STATE_RUNNING, "deveria continuar rodando");
	zassert_equal(ui.data.main.remaining_s, 4, "TICK deve decrementar de 5 para 4");
}

ZTEST(app_fsm, test_work_completes_to_short_break)
{
	struct ui_msg ui = complete_1s_phase();

	zassert_equal(ui.data.main.state, UI_STATE_PAUSED, "fim de fase deve pausar");
	zassert_equal(ui.data.main.phase, UI_PHASE_SHORT_BREAK, "deve ir para SHORT_BREAK");
	zassert_equal(ui.data.main.cycle_done, 1, "cycle_done deve incrementar");

	zassert_ok(k_sem_take(&buzzer_sem, K_MSEC(500)), "buzzer nao acionado");
	zassert_equal(last_buzzer.cmd, UI_BUZZER_ON, "buzzer deveria ligar no fim da fase");
}

ZTEST(app_fsm, test_short_break_completes_to_work)
{
	complete_1s_phase(); 

	struct ui_msg ui = complete_1s_phase();

	zassert_equal(ui.data.main.state, UI_STATE_PAUSED, "deve pausar apos o break");
	zassert_equal(ui.data.main.phase, UI_PHASE_WORK, "break curto deve voltar para WORK");
}

ZTEST(app_fsm, test_long_break_resets_cycle)
{
	/* Avanca ate LONG_BREAK. */
	struct ui_msg ui = {0};
	for (int i = 0; i < EXPECTED_CYCLE_TOTAL; i++) {
		ui = complete_1s_phase();
		if (i < EXPECTED_CYCLE_TOTAL - 1) {
			complete_1s_phase();
		}
	}
	zassert_equal(ui.data.main.phase, UI_PHASE_LONG_BREAK, "pre-condicao: LONG_BREAK");

	/* Completa o LONG_BREAK: volta para WORK zerando o ciclo. */
	ui = complete_1s_phase();

	zassert_equal(ui.data.main.phase, UI_PHASE_WORK, "LONG_BREAK deve voltar para WORK");
	zassert_equal(ui.data.main.cycle_done, 0, "LONG_BREAK deve zerar o ciclo");
}

ZTEST(app_fsm, test_skip_advances_phase)
{
	/* Em WORK/PAUSED (pos-reset), SKIP e tratado por paused_run. */
	struct app_fsm_evt_data e = {.id = APP_FSM_EVENT_SKIP};
	struct ui_msg ui = pub_wait(&e);

	zassert_equal(ui.data.main.state, UI_STATE_PAUSED, "SKIP deve manter pausado");
	zassert_equal(ui.data.main.phase, UI_PHASE_SHORT_BREAK, "SKIP deve avancar de fase");
	zassert_equal(ui.data.main.cycle_done, 1, "SKIP de WORK conta como ciclo concluido");
}

ZTEST(app_fsm, test_orientation_pauses_running)
{
	reset_config(5, 5, 5);
	struct ui_msg ui = resume();
	zassert_equal(ui.data.main.state, UI_STATE_RUNNING, "pre-condicao: rodando");

	/* Girar para FRONT/TOP enquanto roda deve pausar (running_run). */
	ui = resume();

	zassert_equal(ui.data.main.state, UI_STATE_PAUSED, "orientacao deveria pausar");
}

ZTEST(app_fsm, test_config_updates_durations)
{
	struct ui_msg ui = reset_config(10, 20, 30);

	zassert_equal(ui.data.main.state, UI_STATE_PAUSED, "CONFIG deve deixar pausado");
	zassert_equal(ui.data.main.phase, UI_PHASE_WORK, "CONFIG deve reiniciar em WORK");
	zassert_equal(ui.data.main.remaining_s, 10, "remaining_s deve refletir novo work_s");
}
