#pragma once

/* Gap between probe rounds while the station has an IP (30–60 s). */
#define CYD_NET_PERIOD_MS     45000
/* How often to recheck the link while it is down. Not a probe round. */
#define CYD_NET_LINK_DOWN_MS  2000
#define CYD_NET_DNS_MS        2000
#define CYD_NET_HTTP_MS       2500
/* Consecutive failed rounds before the globe goes offline. */
#define CYD_NET_FAILS_OFFLINE 2

/* Hysteresis for the globe. `link_up` is station-has-IP. `round_ok` is one
 * probe round (some URL returned HTTP 204). A down link clears the streak
 * and reports offline immediately. One success reports online. Two failed
 * rounds report offline; a single failure keeps the previous value. */
static inline int cyd_net_apply(int online, int *fails, int link_up, int round_ok)
{
    if (!link_up) {
        *fails = 0;
        return 0;
    }
    if (round_ok) {
        *fails = 0;
        return 1;
    }
    *fails += 1;
    if (*fails >= CYD_NET_FAILS_OFFLINE) {
        return 0;
    }
    return online;
}
