#ifndef ZBUS_CHAN_H
#define ZBUS_CHAN_H

#include <zephyr/zbus/zbus.h>

ZBUS_CHAN_DECLARE(app_fsm_evt_chan);
ZBUS_CHAN_DECLARE(ui_cmd_chan);      /* FSM -> UI (atualizacao do display) */

#endif /* ZBUS_CHAN_H */