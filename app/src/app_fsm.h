/**
 * @file app_fsm.h
 *
 * @author Pedro Giló (phvg@ic.ufal.br)
 * @author Thiago Laurentino (tfml@ic.ufal.br)
 * @author Caio Oliveira (cofa@ic.ufal.br)
 *
 * @brief Interface publica da FSM do Pomodoro: eventos e struct de dados de
 *        evento postados no canal zbus da FSM.
 * @version 0.1
 * @date 2026-07-05
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef APP_FSM_H
#define APP_FSM_H

#include "zbus_chan.h"
#include "orientation_service.h"

#include <stdint.h>

/**
 * @brief Evento processado pela FSM do Pomodoro.
 *
 */
enum app_fsm_event {
	APP_FSM_EVENT_HANDLE = 0,          /**< Reservado (nao utilizado). */
	APP_FSM_EVENT_NEXT,                /**< Alterna rodar/pausar o timer. */
	APP_FSM_EVENT_PREVIOUS,            /**< Zera o timer atual. */
	APP_FSM_EVENT_ORIENTATION_CHANGED, /**< Face para cima mudou. */
	APP_FSM_EVENT_TICK,                /**< 1 Hz: decrementa o contador ativo. */
	APP_FSM_EVENT_SKIP,                /**< Pula a fase atual (long press dos dois botoes). */
	APP_FSM_EVENT_CONFIG,              /**< Nova configuracao (duracoes) aplicada via BLE. */
};

/**
 * @brief Dados de um evento postado na FSM.
 *
 */
struct app_fsm_evt_data {
	enum app_fsm_event id; /**< Identificador do evento. */

	union data {
		struct orientation {
			enum orientation_face previous_face; /**< Face anterior. */
			enum orientation_face current_face;  /**< Face atual. */
		} orientation;                               /**< Dados de mudanca de orientacao. */
		struct config_evt_data {
			uint16_t work_s;       /**< Nova duracao WORK (s). */
			uint16_t break_s;      /**< Nova duracao BREAK (s). */
			uint16_t long_break_s; /**< Nova duracao LONG_BREAK (s). */
		} config;
	} data; /**< Carga util especifica do evento. */
};

#endif /* APP_FSM_H */
