/**
 * @file ble_uuids.h
 *
 * @author Pedro Giló (phvg@ic.ufal.br)
 * @author Thiago Laurentino (tfml@ic.ufal.br)
 * @author Caio Oliveira (cofa@ic.ufal.br)
 *
 * @brief UUIDs 128-bit do servico BLE de configuracao das duracoes do zcube:
 *        servico primario e caracteristicas derivadas do UUID base.
 * @version 0.1
 * @date 2026-07-05
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef BLE_UUIDS_H
#define BLE_UUIDS_H

#include <zephyr/bluetooth/uuid.h>

/** @brief UUID base do zcube; o campo de 32 bits identifica cada atributo. */
#define BLE_UUID_ZCUBE(w32) BT_UUID_128_ENCODE((w32), 0xd7e2, 0x4b3a, 0x9c1e, 0x2a4b6c8d0e1f)

/** @brief UUID do servico primario de configuracao. */
#define BLE_UUID_ZCUBE_SVC_VAL BLE_UUID_ZCUBE(0x5f1d0001)

/** @brief UUID da caracteristica de duracao WORK. */
#define BLE_UUID_ZCUBE_WORK_VAL BLE_UUID_ZCUBE(0x5f1d0002)

/** @brief UUID da caracteristica de duracao BREAK. */
#define BLE_UUID_ZCUBE_BREAK_VAL BLE_UUID_ZCUBE(0x5f1d0003)

/** @brief UUID da caracteristica de duracao LONG_BREAK. */
#define BLE_UUID_ZCUBE_LONG_BREAK_VAL BLE_UUID_ZCUBE(0x5f1d0004)

/** @brief UUID da caracteristica de aplicar (reinicia o Pomodoro). */
#define BLE_UUID_ZCUBE_APPLY_VAL BLE_UUID_ZCUBE(0x5f1d0005)

/** @brief UUID da caracteristica de stream da fase ativa. */
#define BLE_UUID_ZCUBE_ACTIVE_PHASE_VAL BLE_UUID_ZCUBE(0x5f1d0006)

/** @brief UUID da caracteristica de stream do tempo restante. */
#define BLE_UUID_ZCUBE_REMAINING_VAL BLE_UUID_ZCUBE(0x5f1d0007)

/** @brief Ponteiro para o UUID do servico primario. */
#define BLE_UUID_ZCUBE_SVC BT_UUID_DECLARE_128(BLE_UUID_ZCUBE_SVC_VAL)

/** @brief Ponteiro para o UUID da caracteristica WORK. */
#define BLE_UUID_ZCUBE_WORK BT_UUID_DECLARE_128(BLE_UUID_ZCUBE_WORK_VAL)

/** @brief Ponteiro para o UUID da caracteristica BREAK. */
#define BLE_UUID_ZCUBE_BREAK BT_UUID_DECLARE_128(BLE_UUID_ZCUBE_BREAK_VAL)

/** @brief Ponteiro para o UUID da caracteristica LONG_BREAK. */
#define BLE_UUID_ZCUBE_LONG_BREAK BT_UUID_DECLARE_128(BLE_UUID_ZCUBE_LONG_BREAK_VAL)

/** @brief Ponteiro para o UUID da caracteristica APPLY. */
#define BLE_UUID_ZCUBE_APPLY BT_UUID_DECLARE_128(BLE_UUID_ZCUBE_APPLY_VAL)

/** @brief Ponteiro para o UUID da caracteristica de fase ativa. */
#define BLE_UUID_ZCUBE_ACTIVE_PHASE BT_UUID_DECLARE_128(BLE_UUID_ZCUBE_ACTIVE_PHASE_VAL)

/** @brief Ponteiro para o UUID da caracteristica de tempo restante. */
#define BLE_UUID_ZCUBE_REMAINING BT_UUID_DECLARE_128(BLE_UUID_ZCUBE_REMAINING_VAL)

#endif /* BLE_UUIDS_H */
