/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

/*
 * softAP to PPPoS Example (network_dce)
*/

#pragma once

#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MODEM_UART_BAUD 115200
#define MODEM_UART_TXD GPIO_NUM_48
#define MODEM_UART_RXD GPIO_NUM_47
#define MODEM_UART_RTS -1
#define MODEM_UART_CTS -1
#define MODEM_PWR_EN GPIO_NUM_16
#define MODEM_PWR_KEY GPIO_NUM_21

#define MODEM_CONNECT_BIT BIT0
#define MODEM_DISCONNECT_BIT BIT1
#define MODEM_GOT_DATA_BIT BIT2

#define ESP_MODEM_DTE_EDGEBOX_CONFIG() \
    {                                  \
        .dte_buffer_size = CONFIG_GATEWAY_MODEM_DTE_BUFFER_SIZE,        \
        .task_stack_size = CONFIG_GATEWAY_MODEM_UART_EVENT_TASK_STACK_SIZE,       \
        .task_priority = CONFIG_GATEWAY_MODEM_UART_EVENT_TASK_PRIORITY,            \
        .uart_config = {               \
            .port_num = UART_NUM_1,                 \
            .data_bits = UART_DATA_8_BITS,          \
            .stop_bits = UART_STOP_BITS_1,          \
            .parity = UART_PARITY_DISABLE,          \
            .flow_control = ESP_MODEM_FLOW_CONTROL_NONE,\
            .source_clk = ESP_MODEM_DEFAULT_UART_CLK,   \
            .baud_rate = MODEM_UART_BAUD,                    \
            .tx_io_num = MODEM_UART_TXD,                        \
            .rx_io_num = MODEM_UART_RXD,                        \
            .rts_io_num = MODEM_UART_RTS,                       \
            .cts_io_num = MODEM_UART_CTS,                       \
            .rx_buffer_size = CONFIG_GATEWAY_MODEM_UART_RX_BUFFER_SIZE,                 \
            .tx_buffer_size = CONFIG_GATEWAY_MODEM_UART_TX_BUFFER_SIZE,                  \
            .event_queue_size = CONFIG_GATEWAY_MODEM_UART_EVENT_QUEUE_SIZE,                 \
       },                                           \
    }

/**
 * @brief Initialize a singleton covering the PPP network provided by the connected modem device
 *
 * @param netif Already created network interface in PPP mode
 *
 * @return ESP_OK on success
 */
esp_err_t modem_init_network(esp_netif_t *netif, const char * apn);

/**
 * @brief Destroys the single network DCE
 */
void modem_deinit_network();

/**
 * @brief Starts the PPP network
 */
bool modem_start_network();

/**
 * @brief Stops the PPP network
 */
bool modem_stop_network();

bool modem_check_sync();

void modem_reset();

bool modem_check_signal();

#ifdef __cplusplus
}
#endif
