#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ASSISTANT_STR_MAX 64

/*
 * Assistant / smart-home layer.
 *
 * Stores the home-hub connection settings and the chosen "principal" bot in
 * NVS, and produces the JSON consumed by the web app. The chat and device data
 * come from a local mock backend when CONFIG_APP_BOT_BACKEND_MOCK is set (the
 * default), so the whole UI can be exercised without external credentials.
 */

/* True once the user has completed the first-run hub decision. */
bool assistant_onboarding_done(void);

/* Save the home-hub configuration (or a "skip" when enabled is false) and mark
 * onboarding as done. type/addr/user/pass may be empty when skipping. */
esp_err_t assistant_set_hub(bool enabled, const char *type, const char *addr,
                            const char *user, const char *pass);

/* Choose the principal bot shown with the animated avatar. */
esp_err_t assistant_set_principal_bot(const char *bot_id);

/* JSON builders (each always writes a valid JSON document). */
void assistant_state_json(char *buf, size_t buf_len);
void assistant_bots_json(char *buf, size_t buf_len);
void assistant_devices_json(char *buf, size_t buf_len);
void assistant_discover_json(char *buf, size_t buf_len);
void assistant_weather_json(char *buf, size_t buf_len);

/* Produce a reply for a chat message into `out` (JSON-safe plain text). */
void assistant_chat_reply(const char *message, char *out, size_t out_len);

#ifdef __cplusplus
}
#endif
