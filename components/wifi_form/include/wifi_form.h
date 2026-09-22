#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 802.11 limits (excluding the terminating NUL). */
#define WIFI_FORM_SSID_MAX 32
#define WIFI_FORM_PASS_MIN 8
#define WIFI_FORM_PASS_MAX 63

/*
 * Pure, hardware-independent helpers for the Wi-Fi configuration form. Kept in
 * their own component so they can be unit-tested on the host (linux target).
 */

/* Decode an application/x-www-form-urlencoded token ('+' -> space, %XX -> byte)
 * from `src` into `dst` (always NUL-terminated, truncated to dst_size). */
void wifi_form_url_decode(const char *src, char *dst, size_t dst_size);

/* Extract and URL-decode the value of `key` from an urlencoded body such as
 * "ssid=Home&password=secret". Returns true and fills `out` when the key is
 * present, false otherwise. `out` is always NUL-terminated when out_size > 0. */
bool wifi_form_get_field(const char *body, const char *key, char *out, size_t out_size);

/* Valid SSID: 1..32 characters. */
bool wifi_form_valid_ssid(const char *ssid);

/* Valid password: empty (open network) or 8..63 characters (WPA/WPA2). */
bool wifi_form_valid_password(const char *password);

/* Copy `src` into `dst` escaping characters that are not legal inside a JSON
 * string ("\\", '"' and control chars < 0x20). Always NUL-terminated and never
 * writes past dst_size. Useful for embedding user text in JSON responses. */
void wifi_form_json_escape(const char *src, char *dst, size_t dst_size);

#ifdef __cplusplus
}
#endif
