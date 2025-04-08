#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define pdSECOND pdMS_TO_TICKS(1000)

#include "esp_event.h"
#include "nvs_flash.h"


class Main 
{
public:
    esp_err_t setup(void);
    void loop(void);
};