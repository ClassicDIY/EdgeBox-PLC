#include <Arduino.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <time.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include "RTClib.h"
#include "Log.h"
#include "PLC.h"

using namespace ESP_PLC;

PLC _plc = PLC();
RTC_PCF8563 rtc;
Adafruit_ADS1115 ads; /* Use this for the 16-bit version */

void setup()
{
	Serial.begin(115200);
	while (!Serial) {}

	  // Log ESP32-S3 specifications
	  Serial.println("ESP32-S3 Specifications:");
	  Serial.printf("Chip Model: %s\n", ESP.getChipModel());
	  Serial.printf("Chip Revision: %d\n", ESP.getChipRevision());
	  Serial.printf("Number of CPU Cores: %d\n", ESP.getChipCores());
	  Serial.printf("CPU Frequency: %d MHz\n", ESP.getCpuFreqMHz());
	  Serial.printf("Flash Memory Size: %d MB\n", ESP.getFlashChipSize() / (1024 * 1024));
	  Serial.printf("Flash Frequency: %d MHz\n", ESP.getFlashChipSpeed() / 1000000);
	  Serial.printf("Heap Size: %d KB\n", ESP.getHeapSize() / 1024);
	  Serial.printf("Free Heap: %d KB\n", ESP.getFreeHeap() / 1024);

	  
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
	const esp_task_wdt_config_t wdtConfig = {
		.timeout_ms = WATCHDOG_TIMEOUT * 1000,
		.idle_core_mask = 0, // 0 = both cores
		.trigger_panic = true
	};
	esp_task_wdt_init(&wdtConfig); 
	esp_task_wdt_add(NULL); // Add the current task to the watchdog timer
	logd("Setup Done");
}

void loop()
{
	_plc.Process();
	if (WiFi.isConnected())
	{
		_plc.Monitor();
	}
	esp_task_wdt_reset(); // feed watchdog
}