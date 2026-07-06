/**
 * @file orientation_service.c
 *
 * @author Pedro Giló (phvg@ic.ufal.br)
 * @author Thiago Laurentino (tfml@ic.ufal.br)
 * @author Caio Oliveira (cofa@ic.ufal.br)
 *
 * @brief Servico de orientacao: amostra o acelerometro, filtra (EWMA), classifica
 *        a face dominante com histerese e publica mudancas no canal da FSM.
 * @version 0.1
 * @date 2026-07-05
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "orientation_service.h"
#include "app_fsm.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>
#include <stdint.h>
#include <stdlib.h>

LOG_MODULE_REGISTER(orientation_service, LOG_LEVEL_INF);

/** @brief Tamanho da stack da thread de orientacao (bytes). */
#define ORIENTATION_STACK_SIZE 2048

/** @brief Prioridade da thread de orientacao. */
#define ORIENTATION_PRIORITY 7

/** @brief Periodo de amostragem do acelerometro (ms). */
#define SAMPLE_PERIOD_MS 50

/** @brief Deslocamento do filtro EWMA (peso = 1/2^shift para a amostra nova). */
#define EWMA_SHIFT 2

/** @brief Limiar de gravidade para considerar um eixo dominante (mili-m/s^2). */
#define GRAVITY_THRESHOLD_MMS2 6000

/** @brief Amostras estaveis consecutivas para confirmar uma face (histerese). */
#define STABLE_COUNT 5

/**
 * @brief Eixo do acelerometro.
 *
 */
enum orientation_axis {
	ORIENTATION_AXIS_X = 0,   /**< Eixo X. */
	ORIENTATION_AXIS_Y,       /**< Eixo Y. */
	ORIENTATION_AXIS_Z,       /**< Eixo Z. */
	_ORIENTATION_AXIS_AMOUNT, /**< Quantidade de eixos (marcador). */
};

/**
 * @brief Converte struct sensor_value (val1 inteiro + val2 em micro) para
 *        mili-m/s^2, sem ponto flutuante.
 *
 * @param v [in] Valor de sensor a converter.
 * @return int32_t Aceleracao em mili-m/s^2.
 */
static inline int32_t sv_to_mmps2(const struct sensor_value *v);

/**
 * @brief Classifica a face dominante a partir dos eixos filtrados.
 *
 * @param filt [in] Aceleracoes filtradas por eixo (mili-m/s^2).
 * @return enum orientation_face Face dominante ou ORIENTATION_UNKNOWN se abaixo
 *         do limiar de gravidade.
 */
static enum orientation_face classify(const int32_t filt[_ORIENTATION_AXIS_AMOUNT]);

/**
 * @brief Thread de orientacao: amostra, filtra, classifica e publica mudancas.
 *
 * @param p1 Nao utilizado.
 * @param p2 Nao utilizado.
 * @param p3 Nao utilizado.
 */
static void orientation_service_thread(void *p1, void *p2, void *p3);

/**
 * @brief Ponteiro para o dispositivo do acelerometro (MPU6050).
 *
 */
static const struct device *const accel = DEVICE_DT_GET(DT_ALIAS(accel0));

/**
 * @brief Singleton do servico de orientacao.
 *
 */
static struct orientation_service {
	const enum orientation_face orientation_map[_ORIENTATION_AXIS_AMOUNT]
						   [2]; /**< Eixo/sinal -> face. */
	enum orientation_face current_face;             /**< Ultima face publicada. */
} self = {
	.orientation_map =
		{
			[ORIENTATION_AXIS_X] = {ORIENTATION_FRONT, ORIENTATION_BACK},
			[ORIENTATION_AXIS_Y] = {ORIENTATION_RIGHT, ORIENTATION_LEFT},
			[ORIENTATION_AXIS_Z] = {ORIENTATION_TOP, ORIENTATION_BOTTOM},
		},
	.current_face = ORIENTATION_UNKNOWN,
};

K_THREAD_DEFINE(orientation_service_tid, ORIENTATION_STACK_SIZE, orientation_service_thread, NULL,
		NULL, NULL, ORIENTATION_PRIORITY, 0, 0);

const char *orientation_face_str(enum orientation_face f)
{
	const char *name;

	switch (f) {
	case ORIENTATION_TOP: {
		name = "TOP";
	} break;
	case ORIENTATION_BOTTOM: {
		name = "BOTTOM";
	} break;
	case ORIENTATION_FRONT: {
		name = "FRONT";
	} break;
	case ORIENTATION_BACK: {
		name = "BACK";
	} break;
	case ORIENTATION_RIGHT: {
		name = "RIGHT";
	} break;
	case ORIENTATION_LEFT: {
		name = "LEFT";
	} break;
	case ORIENTATION_UNKNOWN:
	default: {
		name = "UNKNOWN";
	} break;
	}

	return name;
}

static inline int32_t sv_to_mmps2(const struct sensor_value *v)
{
	return v->val1 * 1000 + v->val2 / 1000;
}

static enum orientation_face classify(const int32_t filt[_ORIENTATION_AXIS_AMOUNT])
{
	int dominant = ORIENTATION_AXIS_X;
	int32_t best = abs(filt[ORIENTATION_AXIS_X]);

	for (int i = ORIENTATION_AXIS_Y; i < _ORIENTATION_AXIS_AMOUNT; i++) {
		int32_t m = abs(filt[i]);
		if (m > best) {
			best = m;
			dominant = i;
		}
	}

	if (best < GRAVITY_THRESHOLD_MMS2) {
		return ORIENTATION_UNKNOWN;
	}

	int is_positive = (filt[dominant] > 0);

	return self.orientation_map[dominant][is_positive];
}

static void orientation_service_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	if (!device_is_ready(accel)) {
		LOG_ERR("MPU6050 nao esta pronto; orientation_service encerrando");
		return;
	}

	int32_t filt[_ORIENTATION_AXIS_AMOUNT] = {0};
	bool filt_init = false;

	uint8_t candidate_count = 0;

	enum orientation_face candidate = ORIENTATION_UNKNOWN;

	while (1) {
		struct sensor_value a[_ORIENTATION_AXIS_AMOUNT];

		if (sensor_sample_fetch(accel) == 0 &&
		    sensor_channel_get(accel, SENSOR_CHAN_ACCEL_XYZ, a) == 0) {

			for (int i = 0; i < _ORIENTATION_AXIS_AMOUNT; i++) {
				int32_t raw = sv_to_mmps2(&a[i]);
				if (!filt_init) {
					filt[i] = raw; /* primeira amostra sem transiente */
				} else {
					filt[i] += (raw - filt[i]) >> EWMA_SHIFT;
				}
			}
			filt_init = true;

			enum orientation_face face = classify(filt);

			if ((face == ORIENTATION_UNKNOWN) || (face != candidate)) {
				candidate = face;
				candidate_count = 0;
			} else if (candidate_count < STABLE_COUNT) {
				candidate_count++;
			}

			if ((candidate_count >= STABLE_COUNT) && (candidate != self.current_face)) {
				struct app_fsm_evt_data msg = {
					.id = APP_FSM_EVENT_ORIENTATION_CHANGED,
				};
				msg.data.orientation.current_face = candidate;
				msg.data.orientation.previous_face = self.current_face;

				int rc = zbus_chan_pub(&app_fsm_evt_chan, &msg, K_NO_WAIT);
				if (rc) {
					LOG_WRN("falha ao publicar orientacao: %d", rc);
				} else {
					LOG_INF("orientacao -> %s",
						orientation_face_str(candidate));
					self.current_face = candidate;
				}
			}
		} else {
			LOG_ERR("MPU fetch/get falhou");
		}

		k_msleep(SAMPLE_PERIOD_MS);
	}
}
