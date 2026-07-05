/**
 * @file ui_service.h
 * @author your name (you@domain.com)
 * @brief Interface publica do servico de UI: tipos de mensagem e API de
 *        atualizacao da tela.
 * @version 0.1
 * @date 2026-07-05
 * 
 * @copyright Copyright (c) 2026
 * 
 */

#ifndef UI_SERVICE_H
#define UI_SERVICE_H

#include <stdint.h>

/**
 * @brief Identificador da tela a renderizar.
 *
 */
enum ui_screen_id {
	UI_SCREEN_MAIN = 0, /**< Tela principal do timer. */
	_UI_SCREEN_AMOUNT,  /**< Quantidade de telas (marcador). */
};

/**
 * @brief Estado de operacao do timer.
 *
 */
enum ui_run_state {
	UI_STATE_PAUSED = 0, /**< Timer pausado. */
	UI_STATE_RUNNING,    /**< Timer em execucao. */
	_UI_STATE_AMOUNT,    /**< Quantidade de estados (marcador). */
};

/**
 * @brief Fase atual do ciclo Pomodoro.
 *
 */
enum ui_phase {
	UI_PHASE_WORK = 0,    /**< Fase de trabalho. */
	UI_PHASE_SHORT_BREAK, /**< Pausa curta. */
	UI_PHASE_LONG_BREAK,  /**< Pausa longa. */
	_UI_PHASE_AMOUNT      /**< Quantidade de fases (marcador). */
};

/**
 * @brief Direcao da seta de acao inferior.
 *
 */
enum ui_arrow_dir {
	UI_ARROW_BOTTOM = 0, /**< Aponta para baixo (v) - default. */
	UI_ARROW_TOP,        /**< Aponta para cima (^). */
};

/**
 * @brief Mensagem postada para a FSM sobre renderização da tela
 *
 */
struct ui_msg {
	enum ui_screen_id screen; /**< qual tela renderizar */

	union {
		struct {
			uint32_t remaining_s;        /**< contagem do pomodoro (s) */
			enum ui_run_state state;     /**< rodando ou pausado       */
			enum ui_phase phase;         /**< fase atual do ciclo      */
			char profile_name[12];       /**< perfil ativo (copia)     */
			uint8_t cycle_done;          /**< pomodoros concluidos     */
			uint8_t cycle_total;         /**< total do conjunto (0=off)*/
			enum ui_arrow_dir arrow_dir; /**< direcao da seta de acao  */
		} main;
	} data;
};

/**
 * @brief Publica uma mensagem de atualizacao no canal zbus da UI.
 *
 * @param msg [in] Mensagem com o estado a renderizar.
 * @return int 0 em caso de sucesso ou inteiro negativo em caso de falha.
 */
int ui_service_update(const struct ui_msg *msg);

#endif /* UI_SERVICE_H */
