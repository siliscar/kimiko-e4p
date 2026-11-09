/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
static inline void wifi_transfer_service_poll(void) {}

#include "common.h"
#define WIFI_TRANSFER_STOPPED 0
#define WIFI_TRANSFER_MODE_STA 1
#define WIFI_TRANSFER_MODE_AP 2
typedef struct {int state,mode;bool network_ready;} wifi_transfer_status_t;
typedef struct {int mode;bool network_only;} wifi_transfer_cfg_t;
void wifi_transfer_get_status(wifi_transfer_status_t *s);
int wifi_transfer_get_saved_wifi(char *ssid,bool *configured);
int wifi_transfer_start(const wifi_transfer_cfg_t *cfg);
bool wifi_transfer_try_stop_if_idle(void);
bool wifi_transfer_pause_for_sleep(void);
