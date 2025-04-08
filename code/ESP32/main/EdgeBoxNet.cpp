#include "esp_eth.h"
#include <esp_event.h>
#include "esp_mac.h"
#include "esp_eth_mac.h"
#include "esp_netif_ppp.h"
#include "driver/spi_master.h"
#include "EdgeBoxNet.h"
#include "network_dce.h"
#include "Log.h"
#include "IOT.h"

namespace EDGEBOX
{

static void on_ip_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    if (event_id == IP_EVENT_PPP_GOT_IP || event_id == IP_EVENT_ETH_GOT_IP)
    {
        const esp_netif_ip_info_t *ip_info = &event->ip_info;
        logi("Got IP Address");
        logi("~~~~~~~~~~~");
        logi("IP:" IPSTR, IP2STR(&ip_info->ip));
        logi("IPMASK:" IPSTR, IP2STR(&ip_info->netmask));
        logi("Gateway:" IPSTR, IP2STR(&ip_info->gw));
        logi("~~~~~~~~~~~");
        IOT* instance = static_cast<IOT*>(arg);
        if (instance) {
            instance->GoOnline();
        }
    }
    else if (event_id == IP_EVENT_PPP_LOST_IP)
    {
        logi("Modem Disconnect from PPP Server");
    }
    else if (event_id == IP_EVENT_ETH_LOST_IP)
    {
        logi("Ethernet Disconnect");
    }
    else if (event_id == IP_EVENT_GOT_IP6)
    {
        ip_event_got_ip6_t *event = (ip_event_got_ip6_t *)event_data;
        logi("Got IPv6 address " IPV6STR, IPV62STR(event->ip6_info.ip));
    }
    else 
    {
        logd("IP event! %d", (int)event_id);
    }
}

esp_err_t EdgeBoxNet::ConnectEthernet(IOT* piot)
{
    logd("ConnectEthernet");
    esp_err_t ret = ESP_OK;
    if ((ret = gpio_install_isr_service(0)) != ESP_OK)
    {
        if (ret == ESP_ERR_INVALID_STATE)
        {
            logw("GPIO ISR handler has been already installed");
            ret = ESP_OK; // ISR handler has been already installed so no issues
        }
        else
        {
            logd("GPIO ISR handler install failed");
        }
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
        logd("SPI host #1 init failed");
        return ret;
    }
    uint8_t base_mac_addr[6];
    if ((ret = esp_efuse_mac_get_default(base_mac_addr)) == ESP_OK)
    {
        uint8_t local_mac_1[6];
        esp_derive_local_mac(local_mac_1, base_mac_addr);
        logi("ETH MAC: %02X:%02X:%02X:%02X:%02X:%02X", local_mac_1[0], local_mac_1[1], local_mac_1[2], local_mac_1[3], local_mac_1[4], local_mac_1[5]);
        eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG(); // Init common MAC and PHY configs to default
        eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
        phy_config.phy_addr = 1;
        phy_config.reset_gpio_num = ETH_RST;
        spi_device_interface_config_t spi_devcfg = {
            .command_bits = 16, // Actually it's the address phase in W5500 SPI frame
            .address_bits = 8,  // Actually it's the control phase in W5500 SPI frame
            .mode = 0,
            .clock_speed_hz = 25 * 1000 * 1000,
            .spics_io_num = SS,
            .queue_size = 20,
        };
        spi_device_handle_t spi_handle;
        if ((ret = spi_bus_add_device(SPI2_HOST, &spi_devcfg, &spi_handle)) != ESP_OK)
        {
            loge("spi_bus_add_device failed");
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
        }
        if ((ret = esp_eth_ioctl(eth_handle, ETH_CMD_S_MAC_ADDR, local_mac_1)) != ESP_OK) // set mac address
        {
            logd("SPI Ethernet MAC address config failed");
        }
        esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH(); // Initialize the Ethernet interface
        esp_netif_t* _netif = esp_netif_new(&cfg);
        assert(_netif);
        if ((ret = esp_netif_attach(_netif, esp_eth_new_netif_glue(eth_handle))) != ESP_OK)
        {
            loge("esp_netif_attach failed");
        }
        if ((ret = esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &on_ip_event, piot)) != ESP_OK)
        {
            loge("esp_event_handler_register IP_EVENT->IP_EVENT_ETH_GOT_IP failed");
        }
        if ((ret = esp_eth_start(eth_handle)) != ESP_OK)
        {
            loge("esp_netif_attach failed");
            return ret;
        }
    }
    return ret;
}

void EdgeBoxNet::wakeup_modem(void)
{

    pinMode(MODEM_PWR_KEY, OUTPUT);
    pinMode(MODEM_PWR_EN, OUTPUT);
    digitalWrite(MODEM_PWR_EN, HIGH); // send power to the A7670G
    digitalWrite(MODEM_PWR_KEY, LOW);
    delay(1000); 
    logd("Power on the modem");
    digitalWrite(MODEM_PWR_KEY, HIGH);
    delay(2000); 
    logd("Modem is powered up and ready");
}

void EdgeBoxNet::start_network(void)
{
    EventBits_t bits = 0;
    while ((bits & MODEM_CONNECT_BIT) == 0)
    {
        if (!modem_check_sync())
        {
            logw("Modem does not respond, maybe in DATA mode? ...exiting network mode");
            modem_stop_network();
            if (!modem_check_sync())
            {
                logw("Modem does not respond to AT ...restarting");
                modem_reset();
                logi("Restarted");
            }
            continue;
        }
        if (!modem_check_signal())
        {
            logw("Poor signal ...will check after 5s");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        if (!modem_start_network())
        {
            loge("Modem could not enter network mode ...will retry after 10s");
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
        }
    }
}

esp_err_t EdgeBoxNet::ConnectModem(IOT* pIOT)
{
    logd("ConnectModem");
    esp_err_t ret = ESP_OK;
    wakeup_modem();
    esp_netif_config_t ppp_netif_config = ESP_NETIF_DEFAULT_PPP(); // Initialize lwip network interface in PPP mode
    esp_netif_t* _netif = esp_netif_new(&ppp_netif_config);
    assert(_netif);
    ESP_ERROR_CHECK(modem_init_network(_netif)); // Initialize the PPP network and register for IP event
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, on_ip_event, pIOT));
    start_network();
    logi("Modem has acquired network");
    return ret;
}

} //namespace EDGEBOX