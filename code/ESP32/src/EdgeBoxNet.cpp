

#include <esp_netif.h>
#include "esp_eth.h"
#include <esp_event.h>
#include "esp_mac.h"
#include "esp_eth_mac.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "EdgeBoxNet.h"
#include "Log.h"

#define TAG "EdgeBox"

extern bool tcpipInit();
extern void add_esp_interface_netif(esp_interface_t interface, esp_netif_t* esp_netif); /* from WiFiGeneric */

/** Event handler for IP_EVENT_ETH_GOT_IP */
static void got_ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    logi("Ethernet Got IP Address");
    logi("~~~~~~~~~~~");
    logi("ETHIP:" IPSTR, IP2STR(&ip_info->ip));
    logi("ETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
    logi("ETHGW:" IPSTR, IP2STR(&ip_info->gw));
    logi("~~~~~~~~~~~");
}

esp_err_t EdgeBoxNet::Connect()
{
    logd("EdgeBoxNet Connect");
    esp_err_t ret = ESP_OK;
    if (tcpipInit())
    {
        loge("tcpipInit failed");
    }
    // Create instance(s) of esp-netif for SPI Ethernet(s)
    esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_ETH();
    esp_netif_config_t cfg_spi = {
        .base = &esp_netif_config,
        .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH
    };
    esp_netif_t* eth_netif_spi;
    char if_key_str[10];
    char if_desc_str[10];
    strcpy(if_key_str, "ETH_SPI_1");
    strcpy(if_desc_str, "eth1");
    esp_netif_config.if_key = if_key_str;
    esp_netif_config.if_desc = if_desc_str;
    esp_netif_config.route_prio = 30;
    eth_netif_spi = esp_netif_new(&cfg_spi);
    // Init MAC and PHY configs to default
    eth_mac_config_t mac_config_spi = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config_spi = ETH_PHY_DEFAULT_CONFIG();
    bool gpio_isr_svc_init_by_eth = false;
    ret = gpio_install_isr_service(0);
    if (ret == ESP_OK)
    {
        gpio_isr_svc_init_by_eth = true;
    }
    else if (ret == ESP_ERR_INVALID_STATE)
    {
        logw("GPIO ISR handler has been already installed");
        ret = ESP_OK; // ISR handler has been already installed so no issues
    }
    else
    {
        loge("GPIO ISR handler install failed");
        return ret;
    }
    spi_bus_config_t buscfg = {
        .mosi_io_num = MOSI,
        .miso_io_num = MISO,
        .sclk_io_num = SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    if ((ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO)) != ESP_OK)
    {
        loge("SPI host #1 init failed");
        return ret;
    }
    uint8_t base_mac_addr[6];
    if ((ret = esp_efuse_mac_get_default(base_mac_addr)) == ESP_OK)
    {
        uint8_t local_mac_1[6];
        esp_derive_local_mac(local_mac_1, base_mac_addr);
        logi("ETH MAC: %02X:%02X:%02X:%02X:%02X:%02X", local_mac_1[0], local_mac_1[1], local_mac_1[2], local_mac_1[3], local_mac_1[4], local_mac_1[5]);
        // Init common MAC and PHY configs to default
        eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
        eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();

        // Update PHY config based on board specific configuration
        phy_config.phy_addr = 1;
        phy_config.reset_gpio_num = ETH_RST;

        // Configure SPI interface for specific SPI module
        spi_device_interface_config_t spi_devcfg = {
            .command_bits = 16, // Actually it's the address phase in W5500 SPI frame
            .address_bits = 8,  // Actually it's the control phase in W5500 SPI frame
            .mode = 0,
            .clock_speed_hz = 25 * 1000 * 1000,
            .spics_io_num = SS,
            .queue_size = 20,
        };
        // Init vendor specific MAC config to default, and create new SPI Ethernet MAC instance
        // and new PHY instance based on board configuration
        spi_device_handle_t spi_handle;
        if ((ret = spi_bus_add_device(SPI2_HOST, &spi_devcfg, &spi_handle)) != ESP_OK)
        {
            loge("spi_bus_add_device failed");
            return ret;
        }
        eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(spi_handle);
        w5500_config.int_gpio_num = ETH_INT;
        esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
        esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);
        esp_eth_handle_t eth_handle = NULL;
        esp_eth_config_t eth_config_spi = ETH_DEFAULT_CONFIG(mac, phy);
        if ((ret = esp_eth_driver_install(&eth_config_spi, &eth_handle)) != ESP_OK)
        {
            loge("esp_eth_driver_install failed");
            return ret;
        }
        // The SPI Ethernet module might not have a burned factory MAC address, we can set it manually.
        if ((ret = esp_eth_ioctl(eth_handle, ETH_CMD_S_MAC_ADDR, local_mac_1)) != ESP_OK)
        {
            loge("SPI Ethernet MAC address config failed");
            return ret;
        }
        // Initialize the Ethernet interface
        esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
        esp_netif_t *eth_netif = esp_netif_new(&cfg);
        assert(eth_netif);
        if ((ret = esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle))) != ESP_OK)
        {
            loge("esp_netif_attach failed");
            return ret;
        }
        if ((ret = esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL)) != ESP_OK)
        {
            loge("esp_netif_attach IP_EVENT_ETH_GOT_IP failed");
            return ret;
        }
        /* attach to WiFiGeneric to receive events */
        add_esp_interface_netif(ESP_IF_ETH, eth_netif);
        // Start Ethernet
        if ((ret = esp_eth_start(eth_handle)) != ESP_OK)
        {
            loge("esp_netif_attach failed");
            return ret;
        }
     }
    return ret;
}
