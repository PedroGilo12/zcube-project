/**
 * @file app_fsm.h
 * @author your name (you@domain.com)
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
			enum orientation_face
				current_face; /**< Face atual (p/ ORIENTATION_CHANGED). */
		} orientation;                /**< Dados de mudanca de orientacao. */
	} data;                               /**< Carga util especifica do evento. */
};

#endif /* APP_FSM_H */
