#pragma once

#include <stdio.h>
#include <string.h>

/* Home Assistant weather.* state -> short ASCII pt-BR for the panel font.
 * Returns 1 when `out` holds a phrase. Unknown/empty states return 0. */
static inline int cyd_weather_phrase(const char *ha, char *out, size_t n)
{
    static const struct {
        const char *en;
        const char *pt;
    } map[] = {
        {"lightning-rainy", "tempestade"},
        {"snowy-rainy", "neve"},
        {"clear-night", "noite limpa"},
        {"partlycloudy", "parcial"},
        {"windy-variant", "vento"},
        {"pouring", "chuva forte"},
        {"cloudy", "nublado"},
        {"rainy", "chuva"},
        {"sunny", "sol"},
        {"fog", "neblina"},
        {"hail", "granizo"},
        {"lightning", "raios"},
        {"snowy", "neve"},
        {"windy", "vento"},
        {"exceptional", "atencao"},
    };
    size_t i, o;

    if (!out || n == 0) {
        return 0;
    }
    out[0] = '\0';
    if (!ha || !ha[0]) {
        return 0;
    }
    if (strcmp(ha, "unknown") == 0 || strcmp(ha, "unavailable") == 0 || strcmp(ha, "none") == 0) {
        return 0;
    }
    for (i = 0; i < sizeof map / sizeof map[0]; i++) {
        if (strcmp(ha, map[i].en) == 0) {
            snprintf(out, n, "%s", map[i].pt);
            return 1;
        }
    }
    o = 0;
    for (const unsigned char *p = (const unsigned char *)ha; *p && o + 1 < n && o < 14; p++) {
        unsigned char c = *p;
        if (c >= 'A' && c <= 'Z') {
            c = (unsigned char)(c - 'A' + 'a');
        }
        if ((c >= 'a' && c <= 'z') || c == ' ') {
            out[o++] = (char)c;
        }
    }
    out[o] = '\0';
    return o > 0;
}
