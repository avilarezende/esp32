#include "qemu_eth.h"

#if CONFIG_ETH_USE_OPENETH

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_eth_mac_openeth.h"

static const char *TAG = "qemu_eth";

esp_err_t qemu_eth_start(void)
{
    /* The default netif and event loop are already created by wifi_manager. */
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&netif_cfg);
    if (eth_netif == NULL) {
        return ESP_FAIL;
    }

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.autonego_timeout_ms = 100; /* QEMU link comes up immediately */

    esp_eth_mac_t *mac = esp_eth_mac_new_openeth(&mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_dp83848(&phy_config);
    if (mac == NULL || phy == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth_handle = NULL;
    esp_err_t err = esp_eth_driver_install(&config, &eth_handle);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "openeth driver install failed: %s", esp_err_to_name(err));
        return err;
    }

    uint8_t mac_addr[6] = {0};
    esp_read_mac(mac_addr, ESP_MAC_ETH);
    esp_eth_ioctl(eth_handle, ETH_CMD_S_MAC_ADDR, mac_addr);

    err = esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle));
    if (err != ESP_OK) {
        return err;
    }

    err = esp_eth_start(eth_handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "emulated Ethernet started (QEMU); portal reachable over it");
    }
    return err;
}

#else /* !CONFIG_ETH_USE_OPENETH */

esp_err_t qemu_eth_start(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
