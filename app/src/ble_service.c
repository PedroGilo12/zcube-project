/**
 * @file ble_service.c
 *
 * @author Pedro Giló (phvg@ic.ufal.br)
 * @author Thiago Laurentino (tfml@ic.ufal.br)
 * @author Caio Oliveira (cofa@ic.ufal.br)
 *
 * @brief Servico BLE das duracoes do Pomodoro: servico GATT para ler/escrever
 *        WORK/BREAK/LONG_BREAK (persistidas em NVS via Settings), aplicar a config
 *        (reinicia a FSM pelo zbus) e transmitir por notify a fase ativa e o tempo
 *        restante (derivados do canal de UI).
 * @version 0.1
 * @date 2026-07-05
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "ble_service.h"
#include "ble_uuids.h"
#include "app_fsm.h"
#include "ui_service.h"
#include "zbus_chan.h"

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/att.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(ble_service, LOG_LEVEL_DBG);

/** @brief Tamanho da stack da thread do servico BLE (bytes). */
#define BLE_SERVICE_STACK_SIZE 4096

/** @brief Prioridade da thread do servico BLE. */
#define BLE_SERVICE_PRIORITY 7

/** @brief Profundidade da fila do subscriber do canal de UI. */
#define BLE_SERVICE_QUEUE_DEPTH 8

/** @brief Duracao default de fabrica do WORK (min). */
#define BLE_SERVICE_DEFAULT_WORK_MIN 1

/** @brief Duracao default de fabrica do BREAK (min). */
#define BLE_SERVICE_DEFAULT_BREAK_MIN 1

/** @brief Duracao default de fabrica do LONG_BREAK (min). */
#define BLE_SERVICE_DEFAULT_LONG_BREAK_MIN 1

/** @brief Duracao minima aceita numa escrita (min). */
#define BLE_SERVICE_MIN_MINUTES 1

/** @brief Duracao maxima aceita numa escrita (min). */
#define BLE_SERVICE_MAX_MINUTES 60

/** @brief Segundos por minuto (conversao para a FSM). */
#define BLE_SERVICE_SECONDS_PER_MIN 60

/** @brief Subtree do Settings do modulo. */
#define BLE_SERVICE_SETTINGS_SUBTREE "zcube"

/** @brief Chave do Settings (relativa ao subtree) com o blob das 3 duracoes. */
#define BLE_SERVICE_SETTINGS_KEY "cfg"

/** @brief Caminho completo da chave usada em settings_save_one. */
#define BLE_SERVICE_SETTINGS_PATH BLE_SERVICE_SETTINGS_SUBTREE "/" BLE_SERVICE_SETTINGS_KEY

/** @brief Tamanho do blob persistido: 3 duracoes u16 little-endian (min). */
#define BLE_SERVICE_BLOB_LEN 6

/** @brief Indice do atributo de valor da fase ativa na tabela do servico. */
#define BLE_SERVICE_ATTR_PHASE_IDX 10

/** @brief Indice do atributo de valor do tempo restante na tabela do servico. */
#define BLE_SERVICE_ATTR_REMAINING_IDX 13

/**
 * @brief Callback de mudanca de CCC (subscribe/unsubscribe de notify).
 *
 * @param attr Atributo CCC alterado.
 * @param value Novo valor do CCC.
 */
static void ble_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value);

/**
 * @brief Read GATT de um u16 apontado por attr->user_data (little-endian).
 *
 * @param conn Conexao que le.
 * @param attr Atributo cujo user_data aponta para um uint16_t.
 * @param buf [out] Buffer de saida do ATT.
 * @param len Tamanho do buffer.
 * @param offset Deslocamento da leitura.
 * @return ssize_t Bytes lidos ou codigo de erro ATT.
 */
static ssize_t ble_read_u16(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			    uint16_t len, uint16_t offset);

/**
 * @brief Write GATT de uma duracao (u16 min) apontada por attr->user_data; persiste.
 *
 * @param conn Conexao que escreve.
 * @param attr Atributo cujo user_data aponta para um uint16_t (minutos).
 * @param buf [in] Dados recebidos (u16 little-endian, em minutos).
 * @param len Tamanho dos dados.
 * @param offset Deslocamento da escrita.
 * @param flags Flags do write.
 * @return ssize_t Bytes escritos ou codigo de erro ATT.
 */
static ssize_t ble_write_duration(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				  const void *buf, uint16_t len, uint16_t offset, uint8_t flags);

/**
 * @brief Read GATT da fase ativa (u8).
 *
 * @param conn Conexao que le.
 * @param attr Atributo lido.
 * @param buf [out] Buffer de saida do ATT.
 * @param len Tamanho do buffer.
 * @param offset Deslocamento da leitura.
 * @return ssize_t Bytes lidos ou codigo de erro ATT.
 */
static ssize_t ble_read_phase(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			      uint16_t len, uint16_t offset);

/**
 * @brief Write GATT de apply: publica a config no zbus para a FSM reiniciar.
 *
 * @param conn Conexao que escreve.
 * @param attr Atributo escrito.
 * @param buf [in] Deve conter um unico byte igual a 1.
 * @param len Tamanho dos dados.
 * @param offset Deslocamento da escrita.
 * @param flags Flags do write.
 * @return ssize_t Bytes escritos ou codigo de erro ATT.
 */
static ssize_t ble_write_apply(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			       const void *buf, uint16_t len, uint16_t offset, uint8_t flags);

/**
 * @brief Persiste as 3 duracoes atuais como um blob em NVS via Settings.
 *
 * @return int 0 em sucesso, negativo em falha.
 */
static int ble_service_save(void);

/**
 * @brief Publica a config atual (3 duracoes) no canal da FSM.
 *
 * @param timeout Timeout do zbus_chan_pub.
 */
static void ble_service_publish_config(k_timeout_t timeout);

/**
 * @brief Handler de Settings: carrega o blob das 3 duracoes para o singleton.
 *
 * @param key Chave relativa ao subtree.
 * @param len Tamanho do dado no backend.
 * @param read_cb Funcao para ler o dado do backend.
 * @param cb_arg Argumento opaco do backend.
 * @return int 0 em sucesso, negativo em falha.
 */
static int ble_settings_set(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg);

/**
 * @brief Envia notify da fase ativa e do tempo restante aos clientes inscritos.
 */
static void ble_service_notify(void);

/**
 * @brief Thread do servico BLE: inicializa Settings/BT/advertising e transmite
 *        o estado da FSM (fase ativa e tempo restante) por notify.
 *
 * @param p1 Nao utilizado.
 * @param p2 Nao utilizado.
 * @param p3 Nao utilizado.
 */
static void ble_service_thread(void *p1, void *p2, void *p3);

/**
 * @brief Singleton do servico BLE.
 *
 */
static struct ble_service {
	uint16_t work_min;       /**< Duracao WORK (min). */
	uint16_t break_min;      /**< Duracao BREAK (min). */
	uint16_t long_break_min; /**< Duracao LONG_BREAK (min). */
	uint16_t remaining_s;    /**< Ultimo tempo restante recebido do canal de UI (s). */
	uint8_t active_phase;    /**< Ultima fase recebida do canal de UI (enum ui_phase). */
} self = {
	.work_min = BLE_SERVICE_DEFAULT_WORK_MIN,
	.break_min = BLE_SERVICE_DEFAULT_BREAK_MIN,
	.long_break_min = BLE_SERVICE_DEFAULT_LONG_BREAK_MIN,
	.remaining_s = BLE_SERVICE_DEFAULT_WORK_MIN * BLE_SERVICE_SECONDS_PER_MIN,
	.active_phase = UI_PHASE_WORK,
};

/**
 * @brief Handler de Settings do modulo (subtree "zcube").
 *
 */
static struct settings_handler ble_settings_handler = {
	.name = BLE_SERVICE_SETTINGS_SUBTREE,
	.h_set = ble_settings_set,
};

/**
 * @brief Dados de advertising: flags e nome completo do dispositivo.
 *
 */
static const struct bt_data ble_adv_data[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

BT_GATT_SERVICE_DEFINE(
	zcube_svc,
	 BT_GATT_PRIMARY_SERVICE(BLE_UUID_ZCUBE_SVC),
	BT_GATT_CHARACTERISTIC(BLE_UUID_ZCUBE_WORK, BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, ble_read_u16,
			       ble_write_duration, &self.work_min),
	BT_GATT_CHARACTERISTIC(BLE_UUID_ZCUBE_BREAK, BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, ble_read_u16,
			       ble_write_duration, &self.break_min),
	BT_GATT_CHARACTERISTIC(BLE_UUID_ZCUBE_LONG_BREAK, BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, ble_read_u16,
			       ble_write_duration, &self.long_break_min),
	BT_GATT_CHARACTERISTIC(BLE_UUID_ZCUBE_APPLY, BT_GATT_CHRC_WRITE, BT_GATT_PERM_WRITE, NULL,
			       ble_write_apply, NULL),
	BT_GATT_CHARACTERISTIC(BLE_UUID_ZCUBE_ACTIVE_PHASE, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, ble_read_phase, NULL, NULL),
	BT_GATT_CCC(ble_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(BLE_UUID_ZCUBE_REMAINING, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, ble_read_u16, NULL, &self.remaining_s),
	BT_GATT_CCC(ble_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE), );

ZBUS_SUBSCRIBER_DEFINE(ble_service_subscriber, BLE_SERVICE_QUEUE_DEPTH);

ZBUS_CHAN_ADD_OBS(ui_cmd_chan, ble_service_subscriber, 3);

K_THREAD_DEFINE(ble_service_tid, BLE_SERVICE_STACK_SIZE, ble_service_thread, NULL, NULL, NULL,
		BLE_SERVICE_PRIORITY, 0, 0);

static void ble_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	LOG_DBG("CCC alterado: 0x%04x", value);
}

static ssize_t ble_read_u16(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			    uint16_t len, uint16_t offset)
{
	const uint16_t *field = attr->user_data;
	uint8_t le[sizeof(uint16_t)];

	sys_put_le16(*field, le);
	return bt_gatt_attr_read(conn, attr, buf, len, offset, le, sizeof(le));
}

static ssize_t ble_write_duration(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				  const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(flags);

	uint16_t *field = attr->user_data;

	if (offset != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	if (len != sizeof(uint16_t)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	uint16_t v = sys_get_le16(buf);

	if (v < BLE_SERVICE_MIN_MINUTES || v > BLE_SERVICE_MAX_MINUTES) {
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}

	*field = v;

	int rc = ble_service_save();

	if (rc) {
		LOG_ERR("Falha ao persistir config: %d", rc);
	}

	return len;
}

static ssize_t ble_read_phase(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			      uint16_t len, uint16_t offset)
{
	uint8_t v = self.active_phase;

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &v, sizeof(v));
}

static ssize_t ble_write_apply(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			       const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (offset != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	if (len != sizeof(uint8_t)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	uint8_t v = *(const uint8_t *)buf;

	if (v != 1) {
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}

	ble_service_publish_config(K_MSEC(50));
	return len;
}

static int ble_service_save(void)
{
	uint8_t blob[BLE_SERVICE_BLOB_LEN];

	sys_put_le16(self.work_min, &blob[0]);
	sys_put_le16(self.break_min, &blob[2]);
	sys_put_le16(self.long_break_min, &blob[4]);

	return settings_save_one(BLE_SERVICE_SETTINGS_PATH, blob, sizeof(blob));
}

static void ble_service_publish_config(k_timeout_t timeout)
{
	struct app_fsm_evt_data msg = {
		.id = APP_FSM_EVENT_CONFIG,
		.data.config = {
			.work_s = (uint16_t)(self.work_min * BLE_SERVICE_SECONDS_PER_MIN),
			.break_s = (uint16_t)(self.break_min * BLE_SERVICE_SECONDS_PER_MIN),
			.long_break_s = (uint16_t)(self.long_break_min * BLE_SERVICE_SECONDS_PER_MIN),
		},
	};

	int rc = zbus_chan_pub(&app_fsm_evt_chan, &msg, timeout);

	if (rc) {
		LOG_ERR("Falha ao publicar config: %d", rc);
	}
}

static int ble_settings_set(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	if (strcmp(key, BLE_SERVICE_SETTINGS_KEY) != 0) {
		return -ENOENT;
	}

	if (len != BLE_SERVICE_BLOB_LEN) {
		LOG_WRN("Blob de config com tamanho inesperado: %u", (unsigned int)len);
		return -EINVAL;
	}

	uint8_t blob[BLE_SERVICE_BLOB_LEN];
	ssize_t rd = read_cb(cb_arg, blob, sizeof(blob));

	if (rd != (ssize_t)sizeof(blob)) {
		LOG_ERR("Falha ao ler config do backend: %d", (int)rd);
		return -EIO;
	}

	self.work_min = sys_get_le16(&blob[0]);
	self.break_min = sys_get_le16(&blob[2]);
	self.long_break_min = sys_get_le16(&blob[4]);

	return 0;
}

static void ble_service_notify(void)
{
	uint8_t phase = self.active_phase;
	uint8_t remaining[sizeof(uint16_t)];

	sys_put_le16(self.remaining_s, remaining);

	(void)bt_gatt_notify(NULL, &zcube_svc.attrs[BLE_SERVICE_ATTR_PHASE_IDX], &phase,
			     sizeof(phase));
	(void)bt_gatt_notify(NULL, &zcube_svc.attrs[BLE_SERVICE_ATTR_REMAINING_IDX], remaining,
			     sizeof(remaining));
}

static void ble_service_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	const struct zbus_channel *chan;

	int rc = settings_subsys_init();

	if (rc) {
		LOG_ERR("settings_subsys_init falhou: %d", rc);
		return;
	}

	rc = settings_register(&ble_settings_handler);
	if (rc) {
		LOG_ERR("settings_register falhou: %d", rc);
		return;
	}

	rc = settings_load();
	if (rc) {
		LOG_WRN("settings_load falhou: %d (usando defaults)", rc);
	}

	rc = bt_enable(NULL);
	if (rc) {
		LOG_ERR("bt_enable falhou: %d", rc);
		return;
	}

	rc = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ble_adv_data, ARRAY_SIZE(ble_adv_data), NULL,
			     0);
	if (rc) {
		LOG_ERR("bt_le_adv_start falhou: %d", rc);
		return;
	}

	LOG_INF("Servico BLE 'zcube' no ar (work=%u break=%u long=%u min)", self.work_min,
		self.break_min, self.long_break_min);

	/* Tira a FSM do default de fabrica com os valores carregados do NVS. */
	ble_service_publish_config(K_MSEC(200));

	while (!zbus_sub_wait(&ble_service_subscriber, &chan, K_FOREVER)) {
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

		self.active_phase = (uint8_t)msg.data.main.phase;
		self.remaining_s = (uint16_t)msg.data.main.remaining_s;
		ble_service_notify();
	}
}
