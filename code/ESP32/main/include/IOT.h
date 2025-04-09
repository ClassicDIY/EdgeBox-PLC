#pragma once
#include <Arduino.h>
#include "ArduinoJson.h"
#include <EEPROM.h>
#include <AsyncTCP.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ModbusServerTCPasync.h>
#include "mqtt_client.h"
#include "time.h"
#include <sstream>
#include <string>
#include "Defines.h"
#include "Enumerations.h"
#include "OTA.h"
#include "IOTServiceInterface.h"
#include "IOTCallbackInterface.h"

namespace EDGEBOX
{
    class IOT : public IOTServiceInterface
    {
    public:
        IOT() {};
        void Init(IOTCallbackInterface *iotCB, AsyncWebServer *pwebServer);

        void Run();
        boolean Publish(const char *subtopic, const char *value, boolean retained = false);
        boolean Publish(const char *subtopic, JsonDocument &payload, boolean retained = false);
        boolean Publish(const char *subtopic, float value, boolean retained = false);
        boolean PublishMessage(const char *topic, JsonDocument &payload, boolean retained);
        boolean PublishHADiscovery(JsonDocument &payload);
        std::string getRootTopicPrefix();
        u_int getUniqueId() { return _uniqueId; };
        std::string getThingName();
        void PublishOnline();
        NetworkState getNetworkState() { return _networkState; }
        IOTCallbackInterface *IOTCB() { return _iotCB; }
        void registerMBWorkers(FunctionCode fc, MBSworker worker);

        void GoOnline();
        
    private:
        OTA _OTA = OTA();
        AsyncWebServer *_pwebServer;
        NetworkState _networkState = Boot;
        NetworkSelection _NetworkSelection = NotConnected;
        bool _blinkStateOn = false;
        String _AP_SSID = TAG;
        String _AP_Password = DEFAULT_AP_PASSWORD;
        String _SSID;
        String _WiFi_Password;
        String _APN;
        bool _useMQTT = false;
        String _mqttServer;
        int16_t _mqttPort = 1883;
        String _mqttUserName;
        String _mqttUserPassword;
        bool _useModbus = false;
        int16_t _modbusPort = 502;
        int16_t _modbusID = 1;
        bool _clientsConfigured = false;
        IOTCallbackInterface *_iotCB;
        u_int _uniqueId = 0; // unique id from mac address NIC segment
        bool _publishedOnline = false;
        unsigned long _lastBlinkTime = 0;
        unsigned long _lastBootTimeStamp = millis();
        unsigned long _waitInAPTimeStamp = millis();
        unsigned long _wifiConnectionStart = 0;
        char _willTopic[STR_LEN*2];
        char _rootTopicPrefix[STR_LEN];
        esp_mqtt_client_handle_t _mqtt_client_handle = 0;
        void GoOffline();
        void saveSettings();
        void loadSettings();
        void SendNetworkSettings(AsyncWebServerRequest *request);
        void ConnectToMQTTServer();
        void HandleMQTT(int32_t event_id, void *event_data);
        void setState(NetworkState newState);
        void wakeup_modem(void);
        void start_network(void);
        esp_err_t ConnectModem();
        esp_err_t ConnectEthernet();
        void HandleIPEvent(int32_t event_id, void *event_data);
        static void mqttReconnectTimerCF(TimerHandle_t xTimer)
        {
            // Retrieve the instance of the class (stored as the timer's ID)
            IOT *instance = static_cast<IOT *>(pvTimerGetTimerID(xTimer));
            if (instance != nullptr)
            {
                instance->ConnectToMQTTServer();
            }
        }
        static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
        {
            IOT* instance = static_cast<IOT*>(handler_args);
            if (instance != nullptr)
            {
                instance->HandleMQTT(event_id, event_data);
            }
        }
        static void on_ip_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
        {
            IOT* instance = static_cast<IOT*>(arg);
            if (instance) {
                instance->HandleIPEvent(event_id, event_data);
            }
        }
    };

} // namespace EDGEBOX
