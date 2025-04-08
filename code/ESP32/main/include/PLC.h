#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ModbusServerTCPasync.h>
#include "Defines.h"
#include "CoilData.h"
#include "AnalogSensor.h"
#include "DigitalSensor.h"
#include "Coil.h"
#include "IOTCallbackInterface.h"

namespace EDGEBOX
{
	class PLC : public IOTCallbackInterface
	{
	
	public:
		PLC() {};
		void setup();
		void Process();
		void onMqttConnect();
		void onMqttMessage(char* topic, JsonDocument& doc);
		void onWiFiConnect();
		void addNetworkSettings(String& page);
		void addNetworkConfigs(String& page);
		void onSubmitForm(AsyncWebServerRequest *request);
	    void onSaveSetting(JsonDocument& doc);
    	void onLoadSetting(JsonDocument& doc);
		
	protected:
		boolean PublishDiscoverySub(const char *component, const char *entityName, const char *jsonElement, const char *device_class, const char *unit_of_meas, const char *icon = "");
		bool ReadyToPublish() {
			return (!_discoveryPublished);
		}

	private:
		boolean _discoveryPublished = false;
		
		String _lastMessagePublished;
		unsigned long _lastPublishTimeStamp = 0;

		Coil _Coils[DO_PINS] = {GPIO_NUM_40, GPIO_NUM_39, GPIO_NUM_38, GPIO_NUM_37, GPIO_NUM_36, GPIO_NUM_35};
		DigitalSensor _DigitalSensors[DI_PINS] = {GPIO_NUM_4, GPIO_NUM_5, GPIO_NUM_6, GPIO_NUM_7};
		AnalogSensor _AnalogSensors[AI_PINS] = {0, 1, 2, 3};

		CoilData _digitalOutputCoils = CoilData(DO_PINS);
		CoilData _digitalInputDiscretes = CoilData(DI_PINS);

		int16_t _digitalInputs = DI_PINS;
		int16_t _analogInputs = AI_PINS;
		unsigned long _lastHeap = 0;
	};
}