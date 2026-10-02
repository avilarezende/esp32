#include "wifi_manager.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"

#if CONFIG_APP_ENABLE_WIFI_RADIO
#include "esp_wifi.h"
#include "esp_mac.h"
#endif

#include "http_server.h"
#include "qemu_eth.h"

static const char *TAG = "wifi_manager";

#define NVS_NAMESPACE "wifi_cfg"
#define NVS_KEY_SSID  "ssid"
#define NVS_KEY_PASS  "pass"

/* Association attempts during the initial join before starting the portal. */
#define WIFI_STA_MAX_RETRY 5

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static wifi_manager_state_t s_state = WIFI_MANAGER_STATE_PROVISIONING;
static char s_ssid[WIFI_MANAGER_SSID_MAXLEN];
static char s_ip[16];
static char s_disconnect_reason[48];
static httpd_handle_t s_http_server;

#if CONFIG_APP_ENABLE_WIFI_RADIO
static EventGroupHandle_t s_wifi_events;
static int s_retry_num;
static bool s_ever_connected;

static const char *reason_to_str(int reason)
{
    switch (reason) {
        case WIFI_REASON_AUTH_EXPIRE:      return "authentication expired";
        case WIFI_REASON_AUTH_FAIL:        return "authentication failed";
        case WIFI_REASON_NO_AP_FOUND:      return "network not found";
        case WIFI_REASON_ASSOC_FAIL:       return "association failed";
        case WIFI_REASON_HANDSHAKE_TIMEOUT:return "wrong password / handshake timeout";
        case WIFI_REASON_BEACON_TIMEOUT:   return "beacon timeout (out of range)";
        case WIFI_REASON_CONNECTION_FAIL:  return "connection failed";
        default:                           return "disconnected";
    }
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base != WIFI_EVENT) {
        return;
    }
    switch (id) {
        case WIFI_EVENT_STA_START:
            /* APSTA with no saved network must not transmit a join. */
            if (s_ssid[0] != '\0') {
                esp_wifi_connect();
            }
            break;
        case WIFI_EVENT_STA_DISCONNECTED: {
            if (s_ssid[0] == '\0') {
                break;
            }
            wifi_event_sta_disconnected_t *ev = (wifi_event_sta_disconnected_t *)data;
            snprintf(s_disconnect_reason, sizeof(s_disconnect_reason), "%s (%d)",
                     reason_to_str(ev->reason), ev->reason);
            if (s_ever_connected) {
                /* Stay online: keep trying to reconnect indefinitely. */
                s_state = WIFI_MANAGER_STATE_CONNECTING;
                ESP_LOGW(TAG, "link lost (%s); reconnecting", s_disconnect_reason);
                esp_wifi_connect();
            } else if (s_retry_num < WIFI_STA_MAX_RETRY) {
                s_retry_num++;
                ESP_LOGW(TAG, "join retry %d/%d (%s)", s_retry_num, WIFI_STA_MAX_RETRY, s_disconnect_reason);
                esp_wifi_connect();
            } else if (s_wifi_events) {
                xEventGroupSetBits(s_wifi_events, WIFI_FAIL_BIT);
            }
            break;
        }
        case WIFI_EVENT_AP_STACONNECTED: {
            wifi_event_ap_staconnected_t *ev = (wifi_event_ap_staconnected_t *)data;
            ESP_LOGI(TAG, "client " MACSTR " joined the config portal", MAC2STR(ev->mac));
            break;
        }
        default:
            break;
    }
}
#endif /* CONFIG_APP_ENABLE_WIFI_RADIO */

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base != IP_EVENT) {
        return;
    }
    if (id == IP_EVENT_STA_GOT_IP || id == IP_EVENT_ETH_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        esp_ip4addr_ntoa(&event->ip_info.ip, s_ip, sizeof(s_ip));
        ESP_LOGI(TAG, "got IP: %s", s_ip);
#if CONFIG_APP_ENABLE_WIFI_RADIO
        if (id == IP_EVENT_STA_GOT_IP) {
            s_retry_num = 0;
            s_ever_connected = true;
            s_state = WIFI_MANAGER_STATE_CONNECTED;
            s_disconnect_reason[0] = '\0';
            if (s_wifi_events) {
                xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
            }
        }
#endif
    }
}

esp_err_t wifi_manager_save_credentials(const char *ssid, const char *password)
{
    if (ssid == NULL || strlen(ssid) == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(handle, NVS_KEY_SSID, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, NVS_KEY_PASS, password ? password : "");
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "stored credentials for SSID '%s'", ssid);
    }
    return err;
}

esp_err_t wifi_manager_load_credentials(char *ssid, size_t ssid_len,
                                        char *password, size_t password_len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }
    size_t len = ssid_len;
    err = nvs_get_str(handle, NVS_KEY_SSID, ssid, &len);
    if (err == ESP_OK) {
        len = password_len;
        err = nvs_get_str(handle, NVS_KEY_PASS, password, &len);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            password[0] = '\0';
            err = ESP_OK;
        }
    }
    nvs_close(handle);
    return err;
}

esp_err_t wifi_manager_forget(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    esp_err_t e1 = nvs_erase_key(handle, NVS_KEY_SSID);
    esp_err_t e2 = nvs_erase_key(handle, NVS_KEY_PASS);
    /* Missing keys are fine. */
    if (e1 != ESP_OK && e1 != ESP_ERR_NVS_NOT_FOUND) err = e1;
    if (e2 != ESP_OK && e2 != ESP_ERR_NVS_NOT_FOUND) err = e2;
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err == ESP_OK) {
        ESP_LOGW(TAG, "cleared stored Wi-Fi credentials");
        s_ssid[0] = '\0';
    }
    return err;
}

void wifi_manager_mark_connected(const char *ssid)
{
    if (ssid) {
        strlcpy(s_ssid, ssid, sizeof(s_ssid));
    }
    s_state = WIFI_MANAGER_STATE_CONNECTED;
}

void wifi_manager_mark_provisioning(void)
{
    s_state = WIFI_MANAGER_STATE_PROVISIONING;
}

wifi_manager_state_t wifi_manager_get_state(void) { return s_state; }
bool wifi_manager_is_connected(void) { return s_state == WIFI_MANAGER_STATE_CONNECTED; }
bool wifi_manager_is_provisioning(void) { return s_state == WIFI_MANAGER_STATE_PROVISIONING; }
const char *wifi_manager_get_ssid(void) { return s_ssid; }
const char *wifi_manager_get_ip(void) { return s_ip; }
const char *wifi_manager_last_disconnect_reason(void) { return s_disconnect_reason; }

int wifi_manager_get_rssi(void)
{
#if CONFIG_APP_ENABLE_WIFI_RADIO
    wifi_ap_record_t ap;
    if (s_state == WIFI_MANAGER_STATE_CONNECTED && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        return ap.rssi;
    }
#endif
    return 0;
}

#if CONFIG_APP_ENABLE_WIFI_RADIO && CONFIG_APP_ENABLE_CYD
/* The CYD regulator resets if the radio transmits at full power while the
 * backlight is on. 34 is 8.5 dBm, enough for a phone next to the board. */
static void calm_cyd_radio(void)
{
    esp_err_t err = esp_wifi_set_max_tx_power(34);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "could not lower TX power (%s)", esp_err_to_name(err));
    }
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
}
#endif

#if CONFIG_APP_ENABLE_WIFI_RADIO && !CONFIG_APP_ENABLE_CYD
static void start_softap(void)
{
    wifi_config_t ap_config = {
        .ap = {
            .ssid = WIFI_MANAGER_AP_SSID,
            .ssid_len = strlen(WIFI_MANAGER_AP_SSID),
            .password = WIFI_MANAGER_AP_PASS,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .channel = 1,
        },
    };
    if (strlen(WIFI_MANAGER_AP_PASS) == 0) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "config AP up: SSID '%s'", WIFI_MANAGER_AP_SSID);
}

static bool try_connect_sta(const char *ssid, const char *password)
{
    ESP_LOGI(TAG, "connecting to stored network '%s'", ssid);
    s_retry_num = 0;
    xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    wifi_config_t sta_config = {0};
    strlcpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid));
    strlcpy((char *)sta_config.sta.password, password, sizeof(sta_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_events,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, portMAX_DELAY);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}
#endif /* CONFIG_APP_ENABLE_WIFI_RADIO && !CONFIG_APP_ENABLE_CYD */

/* Append a JSON-escaped copy of `s` to buf (only " and \ need escaping here). */
static void append_json_escaped(char *buf, size_t buf_len, const char *s)
{
    size_t len = strlen(buf);
    for (size_t i = 0; s[i] && len + 2 < buf_len; i++) {
        if (s[i] == '"' || s[i] == '\\') {
            buf[len++] = '\\';
        }
        buf[len++] = s[i];
    }
    buf[len] = '\0';
}

esp_err_t wifi_manager_scan_json(char *buf, size_t buf_len)
{
    if (buf == NULL || buf_len < 3) {
        return ESP_ERR_INVALID_ARG;
    }
    strlcpy(buf, "[", buf_len);

#if CONFIG_APP_ENABLE_WIFI_RADIO
    wifi_scan_config_t scan_cfg = { .show_hidden = false };
    if (esp_wifi_scan_start(&scan_cfg, true) != ESP_OK) {
        strlcat(buf, "]", buf_len);
        return ESP_OK;
    }
    uint16_t max = 16;
    wifi_ap_record_t recs[16];
    if (esp_wifi_scan_get_ap_records(&max, recs) != ESP_OK) {
        strlcat(buf, "]", buf_len);
        return ESP_OK;
    }
    for (uint16_t i = 0; i < max; i++) {
        char entry[80];
        snprintf(entry, sizeof(entry), "%s{\"ssid\":\"", (i == 0) ? "" : ",");
        strlcat(buf, entry, buf_len);
        append_json_escaped(buf, buf_len, (char *)recs[i].ssid);
        snprintf(entry, sizeof(entry), "\",\"rssi\":%d,\"auth\":%d}",
                 recs[i].rssi, recs[i].authmode);
        strlcat(buf, entry, buf_len);
    }
#else
    /* No radio under QEMU: return placeholder networks so the dynamic list in
     * the portal UI can be demonstrated end to end. */
    const char *demo =
        "{\"ssid\":\"Demo-Home (QEMU)\",\"rssi\":-45,\"auth\":3},"
        "{\"ssid\":\"Demo-Office (QEMU)\",\"rssi\":-67,\"auth\":4},"
        "{\"ssid\":\"Demo-Open (QEMU)\",\"rssi\":-72,\"auth\":0}";
    strlcat(buf, demo, buf_len);
#endif

    strlcat(buf, "]", buf_len);
    return ESP_OK;
}

esp_err_t wifi_manager_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID,
                                                        &on_ip_event, NULL, NULL));

    char ssid[WIFI_MANAGER_SSID_MAXLEN] = {0};
    char pass[WIFI_MANAGER_PASS_MAXLEN] = {0};
    esp_err_t cred_err = wifi_manager_load_credentials(ssid, sizeof(ssid), pass, sizeof(pass));
    bool have_creds = (cred_err == ESP_OK && strlen(ssid) > 0);
    if (have_creds) {
        strlcpy(s_ssid, ssid, sizeof(s_ssid));
    }

#if CONFIG_APP_ENABLE_WIFI_RADIO
    s_wifi_events = xEventGroupCreate();
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &on_wifi_event, NULL, NULL));

#if CONFIG_APP_ENABLE_CYD
    /* Keep ESP32-Setup up the whole time. A stored network is joined in the
     * background; blocking here used to brown out the board before the AP
     * beaconed, so the phone never finished associating. */
    {
        wifi_config_t ap_config = {
            .ap = {
                .ssid = WIFI_MANAGER_AP_SSID,
                .ssid_len = strlen(WIFI_MANAGER_AP_SSID),
                .password = WIFI_MANAGER_AP_PASS,
                .max_connection = 4,
                .authmode = WIFI_AUTH_WPA2_PSK,
                .channel = 1,
            },
        };
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
        if (have_creds) {
            wifi_config_t sta_config = {0};
            strlcpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid));
            strlcpy((char *)sta_config.sta.password, pass, sizeof(sta_config.sta.password));
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
            s_state = WIFI_MANAGER_STATE_CONNECTING;
        } else {
            s_state = WIFI_MANAGER_STATE_PROVISIONING;
        }
        ESP_ERROR_CHECK(esp_wifi_start());
        calm_cyd_radio();
        ESP_LOGI(TAG, "config AP up: SSID '%s'", WIFI_MANAGER_AP_SSID);
    }
#else
    if (have_creds) {
        s_state = WIFI_MANAGER_STATE_CONNECTING;
        if (try_connect_sta(ssid, pass)) {
            ESP_LOGI(TAG, "connected to '%s'", ssid);
        } else {
            ESP_LOGW(TAG, "could not join '%s' (%s); starting portal", ssid, s_disconnect_reason);
            esp_wifi_stop();
            start_softap();
            s_state = WIFI_MANAGER_STATE_PROVISIONING;
        }
    } else {
        ESP_LOGI(TAG, "no stored Wi-Fi credentials; starting portal");
        start_softap();
        s_state = WIFI_MANAGER_STATE_PROVISIONING;
    }
#endif
#else
    /* QEMU build: no Wi-Fi radio. Serve the portal (and, when credentials are
     * stored, a simulated "connected" status page) over the emulated Ethernet
     * interface. */
    ESP_LOGW(TAG, "Wi-Fi radio disabled (QEMU build); using emulated Ethernet");
    qemu_eth_start();
    if (have_creds) {
        s_state = WIFI_MANAGER_STATE_CONNECTED;
        ESP_LOGI(TAG, "found stored credentials for SSID '%s' (simulated connect in QEMU)", ssid);
    } else {
        s_state = WIFI_MANAGER_STATE_PROVISIONING;
        ESP_LOGI(TAG, "no stored Wi-Fi credentials; starting portal");
    }
#endif

    s_http_server = http_config_server_start();
    if (s_http_server == NULL) {
        ESP_LOGE(TAG, "failed to start web server");
    }
    return ESP_OK;
}
