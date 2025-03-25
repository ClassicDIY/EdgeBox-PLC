
#pragma once

#define TAG "ESP_PLC"

#define WATCHDOG_TIMEOUT 10 // time in seconds to trigger the watchdog reset

#define STR_LEN 64
#define EEPROM_SIZE 512
#define AP_BLINK_RATE 600
#define NC_BLINK_RATE 100
// #define AP_TIMEOUT 1000
#define AP_TIMEOUT 30000
#define WIFI_CONNECTION_TIMEOUT 30000
#define DEFAULT_AP_PASSWORD "12345678"

#define ADC_Resolution 65536.0
#define SAMPLESIZE 10
#define MQTT_PUBLISH_RATE_LIMIT 500 // delay between MQTT publishes

#define ASYNC_WEBSERVER_PORT 80
#define DNS_PORT 53

#define DI_PINS 4	// Number of digital input pins
#define DO_PINS 6	// Number of digital output pins
#define AI_PINS 4	// Number of analog input pins
// #define WIFI_STATUS_PIN 43 //LED Pin on the Dev board
#define FACTORY_RESET_PIN 2 // Clear NVRAM

