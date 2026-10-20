#include "cyd_net.h"

#include "sdkconfig.h"

#if CONFIG_APP_ENABLE_CYD

#include <stdatomic.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "wifi_manager.h"

static const char *TAG = "cyd_net";

/* Stop at the first HTTP 204. Redirects are not followed, and HTTP 200
 * (typical captive portal) does not count. */
static const struct {
    const char *host;
    const char *url;
} PROBE[] = {
    {"connectivitycheck.gstatic.com", "http://connectivitycheck.gstatic.com/generate_204"},
    {"www.gstatic.com", "http://www.gstatic.com/generate_204"},
    {"cp.cloudflare.com", "http://cp.cloudflare.com/"},
};

static atomic_int s_ok;
static SemaphoreHandle_t s_dns_sem;
static portMUX_TYPE s_dns_mux = portMUX_INITIALIZER_UNLOCKED;
static int s_dns_gen;
static int *s_dns_out;

static void dns_found(const char *name, const ip_addr_t *addr, void *arg)
{
    int gen = (int)(intptr_t)arg;
    int give = 0;
    (void)name;
    taskENTER_CRITICAL(&s_dns_mux);
    if (gen == s_dns_gen && s_dns_out) {
        *s_dns_out = (addr != NULL) ? 1 : 0;
        s_dns_out = NULL;
        give = 1;
    }
    taskEXIT_CRITICAL(&s_dns_mux);
    if (give && s_dns_sem) {
        xSemaphoreGive(s_dns_sem);
    }
}

/* Resolve `host` with its own timeout. A late callback from a previous
 * attempt cannot publish into the next wait. */
static int dns_resolves(const char *host)
{
    ip_addr_t addr;
    int result = 0;
    int gen;
    err_t err;

    taskENTER_CRITICAL(&s_dns_mux);
    gen = ++s_dns_gen;
    s_dns_out = &result;
    taskEXIT_CRITICAL(&s_dns_mux);
    while (xSemaphoreTake(s_dns_sem, 0) == pdTRUE) {
    }

    err = dns_gethostbyname(host, &addr, dns_found, (void *)(intptr_t)gen);
    if (err == ERR_OK) {
        taskENTER_CRITICAL(&s_dns_mux);
        s_dns_out = NULL;
        taskEXIT_CRITICAL(&s_dns_mux);
        return 1;
    }
    if (err != ERR_INPROGRESS) {
        taskENTER_CRITICAL(&s_dns_mux);
        s_dns_out = NULL;
        taskEXIT_CRITICAL(&s_dns_mux);
        return 0;
    }
    if (xSemaphoreTake(s_dns_sem, pdMS_TO_TICKS(CYD_NET_DNS_MS)) != pdTRUE) {
        taskENTER_CRITICAL(&s_dns_mux);
        s_dns_gen++;
        s_dns_out = NULL;
        taskEXIT_CRITICAL(&s_dns_mux);
        return 0;
    }
    return result;
}

static int http_204(const char *url)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = CYD_NET_HTTP_MS,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    int status;
    esp_err_t err;
    if (!client) {
        return 0;
    }
    err = esp_http_client_perform(client);
    status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return err == ESP_OK && status == 204;
}

static int probe_round(void)
{
    size_t i;
    for (i = 0; i < sizeof PROBE / sizeof PROBE[0]; i++) {
        if (!dns_resolves(PROBE[i].host)) {
            continue;
        }
        if (http_204(PROBE[i].url)) {
            return 1;
        }
    }
    return 0;
}

static void publish(int online)
{
    if (atomic_load(&s_ok) == online) {
        return;
    }
    atomic_store(&s_ok, online);
    ESP_LOGI(TAG, "internet %s", online ? "ok" : "off");
}

static void net_task(void *arg)
{
    int online = 0;
    int fails = 0;
    (void)arg;
    for (;;) {
        int link = wifi_manager_is_connected() ? 1 : 0;
        int round_ok = 0;
        int next;
        if (link) {
            round_ok = probe_round();
        }
        next = cyd_net_apply(online, &fails, link, round_ok);
        publish(next);
        online = next;
        vTaskDelay(pdMS_TO_TICKS(link ? CYD_NET_PERIOD_MS : CYD_NET_LINK_DOWN_MS));
    }
}

int cyd_net_ok(void)
{
    return atomic_load(&s_ok);
}

esp_err_t cyd_net_start(void)
{
    static int started;
    if (started) {
        return ESP_OK;
    }
    s_dns_sem = xSemaphoreCreateBinary();
    if (!s_dns_sem) {
        return ESP_ERR_NO_MEM;
    }
    atomic_store(&s_ok, 0);
    if (xTaskCreate(net_task, "cyd_net", 6144, NULL, 2, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    started = 1;
    return ESP_OK;
}

#else /* !CONFIG_APP_ENABLE_CYD */

int cyd_net_ok(void)
{
    return 0;
}

esp_err_t cyd_net_start(void)
{
    return ESP_OK;
}

#endif
