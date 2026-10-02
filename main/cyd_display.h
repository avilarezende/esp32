#pragma once

#include "esp_err.h"

/* Start the CYD panel task when CONFIG_APP_ENABLE_CYD is set. */
esp_err_t cyd_display_start(void);
