#include <Arduino.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <time.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <Thread.h>
#include <ThreadController.h>
#include "RTClib.h"
#include "main.h"
#include "Log.h"
#include "PLC.h"

using namespace EDGEBOX;

static Main my_main;
PLC _plc = PLC();
RTC_PCF8563 rtc;
Adafruit_ADS1115 ads; /* Use this for the 16-bit version */
ThreadController _controller = ThreadController();
Thread *_workerThread1 = new Thread();
Thread *_workerThread2 = new Thread();
Thread *_workerThread3 = new Thread();

esp_err_t Main::setup()
{
	Serial.begin(115200);
	while (!Serial) {}
	esp_err_t ret = ESP_OK;
 
	logd("------------ESP32-S3 specifications ---------------");
	logd("Chip Model: %s", ESP.getChipModel());
	logd("Chip Revision: %d", ESP.getChipRevision());
	logd("Number of CPU Cores: %d", ESP.getChipCores());
	logd("CPU Frequency: %d MHz", ESP.getCpuFreqMHz());
	logd("Flash Memory Size: %d MB", ESP.getFlashChipSize() / (1024 * 1024));
	logd("Flash Frequency: %d MHz", ESP.getFlashChipSpeed() / 1000000);
	logd("Heap Size: %d KB", ESP.getHeapSize() / 1024);
	logd("Free Heap: %d KB", ESP.getFreeHeap() / 1024);
	logd("------------ESP32-S3 specifications ---------------");

	Wire.begin(SDA, SCL);
	if (!ads.begin(0x48, &Wire))
	{
		loge("Failed to initialize ADS.");
	}
	if (!rtc.begin(&Wire)) {
		loge("Couldn't find RTC");
	}
	if (rtc.lostPower()) {
		logw("RTC is NOT initialized, let's set the time!");
		rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
	}
	// rtc.adjust(DateTime("Apr 16 2020","18:34:56"));
	rtc.start();
	DateTime now = rtc.now();
    logi("Date Time: %s", now.timestamp().c_str());

	_plc.setup();
	// Configure main worker thread
	_workerThread1->onRun([]() { _plc.CleanUp(); });
	_workerThread1->setInterval(5000);
	_controller.add(_workerThread1);
	_workerThread2->onRun([]() { _plc.Monitor(); });
	_workerThread2->setInterval(200);
	_controller.add(_workerThread2);
	_workerThread3->onRun([]() { _plc.Process(); });
	_workerThread3->setInterval(200);
	_controller.add(_workerThread3);
	logd("Setup Done");
	return ret;
}

void Main::loop()
{
	// _plc.Process();
	_controller.run();
}

extern "C" void app_main(void)
{
    logi("Creating default event loop");
    // Initialize esp_netif and default event loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    logi("Initialising NVS");
    ESP_ERROR_CHECK(nvs_flash_init());
    logi("Calling my_main.setup()");
    ESP_ERROR_CHECK(my_main.setup());
    while (true)
    {
        my_main.loop();
    }
}
