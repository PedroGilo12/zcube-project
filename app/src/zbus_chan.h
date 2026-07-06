#ifndef ZBUS_CHAN_H
#define ZBUS_CHAN_H

#include <zephyr/zbus/zbus.h>

ZBUS_CHAN_DECLARE(app_fsm_evt_chan);/* Canal geral de eventos da FSM */
ZBUS_CHAN_DECLARE(ui_cmd_chan);     /* FSM -> UI (atualizacao do display) */
ZBUS_CHAN_DECLARE(ui_buzzer_chan);  /* FSM -> UI (controle do buzzer)     */

#endif /* ZBUS_CHAN_H */
