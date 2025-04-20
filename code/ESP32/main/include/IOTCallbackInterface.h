#pragma once
#include "Arduino.h"
#include "ArduinoJson.h"

class IOTCallbackInterface
{
public:
    virtual void onMqttConnect() = 0;
    virtual void onMqttMessage(char* topic, JsonDocument& doc) = 0;
    virtual void onNetworkConnect() = 0;
    virtual void addApplicationSettings(String& page);
    virtual void addApplicationConfigs(String& page);
    virtual void onSubmitForm(AsyncWebServerRequest *request);
    virtual void onSaveSetting(JsonDocument& doc);
    virtual void onLoadSetting(JsonDocument& doc);
    
};