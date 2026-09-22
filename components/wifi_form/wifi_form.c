#include "wifi_form.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void wifi_form_url_decode(const char *src, char *dst, size_t dst_size)
{
    if (dst_size == 0) {
        return;
    }
    size_t di = 0;
    for (size_t si = 0; src && src[si] != '\0' && di + 1 < dst_size; si++) {
        char c = src[si];
        if (c == '+') {
            dst[di++] = ' ';
        } else if (c == '%' && src[si + 1] && src[si + 2]) {
            int hi = hex_val(src[si + 1]);
            int lo = hex_val(src[si + 2]);
            if (hi >= 0 && lo >= 0) {
                dst[di++] = (char)((hi << 4) | lo);
                si += 2;
            } else {
                dst[di++] = c;
            }
        } else {
            dst[di++] = c;
        }
    }
    dst[di] = '\0';
}

bool wifi_form_get_field(const char *body, const char *key, char *out, size_t out_size)
{
    if (out_size > 0) {
        out[0] = '\0';
    }
    if (!body || !key || out_size == 0) {
        return false;
    }

    size_t key_len = strlen(key);
    const char *p = body;
    while (*p) {
        /* Match "key=" at the start of a token. */
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            const char *val = p + key_len + 1;
            const char *end = strchr(val, '&');
            size_t raw_len = end ? (size_t)(end - val) : strlen(val);

            char raw[256];
            if (raw_len >= sizeof(raw)) {
                raw_len = sizeof(raw) - 1;
            }
            memcpy(raw, val, raw_len);
            raw[raw_len] = '\0';

            wifi_form_url_decode(raw, out, out_size);
            return true;
        }
        /* Advance to the character after the next '&'. */
        const char *amp = strchr(p, '&');
        if (!amp) {
            break;
        }
        p = amp + 1;
    }
    return false;
}

bool wifi_form_valid_ssid(const char *ssid)
{
    if (!ssid) {
        return false;
    }
    size_t len = strlen(ssid);
    return len >= 1 && len <= WIFI_FORM_SSID_MAX;
}

bool wifi_form_valid_password(const char *password)
{
    if (!password) {
        return false;
    }
    size_t len = strlen(password);
    return len == 0 || (len >= WIFI_FORM_PASS_MIN && len <= WIFI_FORM_PASS_MAX);
}

void wifi_form_json_escape(const char *src, char *dst, size_t dst_size)
{
    if (dst_size == 0) {
        return;
    }
    size_t di = 0;
    for (size_t si = 0; src && src[si] != '\0'; si++) {
        unsigned char c = (unsigned char)src[si];
        const char *esc = NULL;
        char ubuf[7];
        switch (c) {
            case '"':  esc = "\\\""; break;
            case '\\': esc = "\\\\"; break;
            case '\n': esc = "\\n";  break;
            case '\r': esc = "\\r";  break;
            case '\t': esc = "\\t";  break;
            default:
                if (c < 0x20) {
                    snprintf(ubuf, sizeof(ubuf), "\\u%04x", c);
                    esc = ubuf;
                }
                break;
        }
        if (esc) {
            size_t elen = strlen(esc);
            if (di + elen >= dst_size) break;
            memcpy(dst + di, esc, elen);
            di += elen;
        } else {
            if (di + 1 >= dst_size) break;
            dst[di++] = (char)c;
        }
    }
    dst[di] = '\0';
}
