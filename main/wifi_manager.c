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

/* Number of association attempts before giving up and starting the portal. */
#define WIFI_STA_MAX_RETRY 5

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static volatile bool s_connected;
static volatile bool s_provisioning;
static httpd_handle_t s_http_server;

#if CONFIG_APP_ENABLE_WIFI_RADIO
static EventGroupHandle_t s_wifi_events;
static int s_retry_num;

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        if (s_retry_num < WIFI_STA_MAX_RETRY) {
            s_retry_num++;
            ESP_LOGW(TAG, "station disconnected, retry %d/%d", s_retry_num, WIFI_STA_MAX_RETRY);
            esp_wifi_connect();
        } else if (s_wifi_events) {
            xEventGroupSetBits(s_wifi_events, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "station got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        s_connected = true;
        if (s_wifi_events) {
            xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)data;
        ESP_LOGI(TAG, "client " MACSTR " joined the config portal", MAC2STR(event->mac));
    }
}
#endif /* CONFIG_APP_ENABLE_WIFI_RADIO */

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
        return err; /* ESP_ERR_NVS_NOT_FOUND when the namespace is empty */
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

bool wifi_manager_is_connected(void)
{
    return s_connected;
}

bool wifi_manager_is_provisioning(void)
{
    return s_provisioning;
}

#if CONFIG_APP_ENABLE_WIFI_RADIO
/* Configure the SoftAP used when no network is joined (real hardware). */
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

    /* APSTA keeps the station interface available so the portal can scan for
     * nearby networks while advertising its own AP. */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "config AP up: SSID '%s' (password '%s')", WIFI_MANAGER_AP_SSID, WIFI_MANAGER_AP_PASS);
}

/* Try to join the network stored in NVS. Returns true on success. */
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
#endif /* CONFIG_APP_ENABLE_WIFI_RADIO */

/* Start the configuration portal: SoftAP (on hardware) and/or the emulated
 * Ethernet interface (under QEMU), plus the HTTP configuration server. */
static void start_provisioning(void)
{
    ESP_LOGI(TAG, "starting '%s' configuration portal", WIFI_MANAGER_AP_SSID);
    s_provisioning = true;

#if CONFIG_APP_ENABLE_WIFI_RADIO
    start_softap();
#endif

#if CONFIG_ETH_USE_OPENETH
    /* Under QEMU there is no Wi-Fi radio, so also bring up the emulated
     * Ethernet interface to make the portal reachable from the host. */
    esp_err_t eth_err = qemu_eth_start();
    if (eth_err != ESP_OK) {
        ESP_LOGD(TAG, "emulated Ethernet not available (%s)", esp_err_to_name(eth_err));
    }
#endif

    s_http_server = http_config_server_start();
    if (s_http_server == NULL) {
        ESP_LOGE(TAG, "failed to start configuration web server");
    }
}

esp_err_t wifi_manager_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    char ssid[WIFI_MANAGER_SSID_MAXLEN] = {0};
    char pass[WIFI_MANAGER_PASS_MAXLEN] = {0};
    esp_err_t cred_err = wifi_manager_load_credentials(ssid, sizeof(ssid), pass, sizeof(pass));

#if CONFIG_APP_ENABLE_WIFI_RADIO
    s_wifi_events = xEventGroupCreate();
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &on_wifi_event, NULL, NULL));

    if (cred_err == ESP_OK && strlen(ssid) > 0) {
        if (try_connect_sta(ssid, pass)) {
            ESP_LOGI(TAG, "connected to '%s'", ssid);
            return ESP_OK;
        }
        ESP_LOGW(TAG, "could not join '%s' after %d retries", ssid, WIFI_STA_MAX_RETRY);
        esp_wifi_stop(); /* reset radio before reconfiguring as SoftAP */
    } else {
        ESP_LOGI(TAG, "no stored Wi-Fi credentials found");
    }
#else
    /* QEMU build: the Wi-Fi radio is disabled. Report stored credentials so the
     * portal's persistence can be verified across reboots, then serve it over
     * the emulated Ethernet interface. */
    ESP_LOGW(TAG, "Wi-Fi radio disabled (QEMU build); serving portal over Ethernet");
    if (cred_err == ESP_OK && strlen(ssid) > 0) {
        ESP_LOGI(TAG, "found stored credentials for SSID '%s'", ssid);
    } else {
        ESP_LOGI(TAG, "no stored Wi-Fi credentials found");
    }
#endif

    start_provisioning();
    return ESP_OK;
}
