#pragma once

#include "esp_err.h"

#include "cyd_net_logic.h"

/* 1 after a confirmed HTTP 204 round, 0 otherwise (including before the
 * first success and whenever the station has no IP). */
int cyd_net_ok(void);

/* Start the probe task. No-op when the CYD panel is not in this build. */
esp_err_t cyd_net_start(void);
