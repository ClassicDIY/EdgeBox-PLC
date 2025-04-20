#include <sys/time.h>
#include <thread>
#include <chrono>
#include <ESPmDNS.h>
#include <SPI.h>
#include <Ethernet.h>
#include <esp_netif.h>
#include <esp_eth.h>
#include <esp_event.h>
#include "esp_mac.h"
#include "esp_eth_mac.h"
#include "esp_netif_ppp.h"
#include "driver/spi_master.h"
#include "network_dce.h"
#include "Log.h"
#include "WebLog.h"
#include "IOT.h"
#include "IOT.html"
#include "HelperFunctions.h"


namespace EDGEBOX
{
	TimerHandle_t mqttReconnectTimer;
	static DNSServer _dnsServer;
	static WebLog _webLog;
	static ModbusServerTCPasync _MBserver;
	static AsyncAuthenticationMiddleware basicAuth;

	void IOT::Init(IOTCallbackInterface *iotCB, AsyncWebServer *pwebServer)
	{
		_iotCB = iotCB;
		_pwebServer = pwebServer;
		pinMode(FACTORY_RESET_PIN, INPUT_PULLUP);
#ifndef LOG_TO_SERIAL_PORT
		pinMode(WIFI_STATUS_PIN, OUTPUT); // use LED if the log level is none (edgeBox shares the LED pin with the serial TX gpio)
#endif
		EEPROM.begin(EEPROM_SIZE);
		if (digitalRead(FACTORY_RESET_PIN) == LOW)
		{
			logi("Factory Reset");
			EEPROM.write(0, 0);
			EEPROM.commit();
		}
		else
		{
			logi("Loading configuration from EEPROM");
			loadSettings();
		}
		mqttReconnectTimer = xTimerCreate("mqttTimer", pdMS_TO_TICKS(8000), pdFALSE, this, mqttReconnectTimerCF);

		WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info)
					 {
			String s;
			JsonDocument doc;
			switch (event)
			{
			case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
				logd("AP_STADISCONNECTED");
				GoOffline();
			break;
			case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
				logd("AP_STAIPASSIGNED");
				GoOnline();
			break;
			case ARDUINO_EVENT_WIFI_STA_GOT_IP:
				logd("STA_GOT_IP");
				doc["IP"] = WiFi.localIP().toString().c_str();
				doc["ApPassword"] = DEFAULT_AP_PASSWORD;
				serializeJson(doc, s);
				s += '\n';
				Serial.printf(s.c_str()); // send json to flash tool
				configTime(0, 0, NTP_SERVER);
				printLocalTime();
				GoOnline();
				break;
			case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
				logw("STA_DISCONNECTED");
				GoOffline();
				break;
			default:
				logd("[WiFi-event] event: %d", event);
				break;
			} });
		// generate unique id from mac address NIC segment
		uint8_t chipid[6];
		esp_efuse_mac_get_default(chipid);
		_uniqueId = chipid[3] << 16;
		_uniqueId += chipid[4] << 8;
		_uniqueId += chipid[5];
		_lastBootTimeStamp = millis();
		_pwebServer->on("/reboot", [this](AsyncWebServerRequest *request)
						{ 
			logd("resetModule");
			String page = reboot_html;
			request->send(200, "text/html", page.c_str());
			delay(3000);
			esp_restart(); });

		_pwebServer->onNotFound([this](AsyncWebServerRequest *request)
								{
			logd("Redirecting from: %s", request->url().c_str());
			String page = redirect_html;
			page.replace("{n}", _SSID);
			IPAddress IP = WiFi.softAPIP();
			String home = IP.toString();
			home += "/settings";
			page.replace("{ip}", home.c_str()); // go directly to settings
			request->send(200, "text/html", page); });

		basicAuth.setUsername("admin");
		basicAuth.setPassword(_AP_Password.c_str());
		basicAuth.setAuthFailureMessage("Authentication failed");
		basicAuth.setAuthType(AsyncAuthType::AUTH_BASIC);
		basicAuth.generateHash();
		_pwebServer->on("/network_config", HTTP_GET, [this](AsyncWebServerRequest *request)
		{
			logd("config");
			String fields = network_config_fields;
			fields.replace("{n}", _AP_SSID);
			fields.replace("{v}", CONFIG_VERSION);
			fields.replace("{AP_SSID}", _AP_SSID);
			fields.replace("{AP_Pw}", _AP_Password);

			fields.replace("{WIFI}", _NetworkSelection == WiFiMode ? "selected" : "");
			fields.replace("{ETH}", _NetworkSelection == EthernetMode ? "selected" : "");
			fields.replace("{4G}", _NetworkSelection == ModemMode ? "selected" : "");

			fields.replace("{SSID}", _SSID);
			fields.replace("{WiFi_Pw}", _WiFi_Password);
			fields.replace("{dhcpChecked}", _useDHCP ? "checked" : "unchecked");

			fields.replace("{ETH_SIP}", _Static_IP);
			fields.replace("{ETH_SM}", _Subnet_Mask);
			fields.replace("{ETH_GW}", _Gateway_IP);

			fields.replace("{APN}", _APN);
			fields.replace("{SIM_USERNAME}", _SIM_Username);
			fields.replace("{SIM_PASSWORD}", _SIM_Password);
			fields.replace("{SIM_PIN}", _SIM_PIN);

			fields.replace("{mqttchecked}", _useMQTT ? "checked" : "unchecked");
			fields.replace("{mqttServer}", _mqttServer);
			fields.replace("{mqttPort}", String(_mqttPort));
			fields.replace("{mqttUser}", _mqttUserName);
			fields.replace("{mqttPw}", _mqttUserPassword);
			fields.replace("{modbuschecked}", _useModbus ? "checked" : "unchecked");
			fields.replace("{modbusPort}", String(_modbusPort));
			fields.replace("{modbusID}", String(_modbusID));
			fields.replace("{inputRegBase}", String(_input_register_base_addr));
			fields.replace("{coilBase}", String(_coil_base_addr));
			fields.replace("{discreteBase}", String(_discrete_input_base_addr));
			Serial.println(fields.c_str());
			String page = network_config_top;
			page.replace("{n}", _AP_SSID);
			page.replace("{v}", CONFIG_VERSION);
			page += fields;
			_iotCB->addApplicationConfigs(page);
			String apply_button = network_config_apply_button;
			page += apply_button;
			page += network_config_links;
			request->send(200, "text/html", page); 
		}).addMiddleware(&basicAuth);

		_pwebServer->on("/submit", HTTP_POST, [this](AsyncWebServerRequest *request)
						{
			logd("submit");
			if (request->hasParam("AP_SSID", true)) {
				_AP_SSID = request->getParam("AP_SSID", true)->value().c_str();
			}
			if (request->hasParam("AP_Pw", true)) {
				_AP_Password = request->getParam("AP_Pw", true)->value().c_str();
			}
			if (request->hasParam("SSID", true)) {
				_SSID = request->getParam("SSID", true)->value().c_str();
			}
			if (request->hasParam("networkSelector", true)) {
				String sel =  request->getParam("networkSelector", true)->value();
				_NetworkSelection = sel == "wifi" ? WiFiMode : sel == "ethernet" ? EthernetMode : ModemMode;
			}
			if (request->hasParam("WiFi_Pw", true)) {
				_WiFi_Password = request->getParam("WiFi_Pw", true)->value().c_str();
			}
			if (request->hasParam("APN", true)) {
				_APN = request->getParam("APN", true)->value().c_str();
			}
			if (request->hasParam("SIM_USERNAME", true)) {
				_SIM_Username = request->getParam("SIM_USERNAME", true)->value().c_str();
			}
			if (request->hasParam("SIM_PASSWORD", true)) {
				_SIM_Password = request->getParam("SIM_PASSWORD", true)->value().c_str();
			}
			if (request->hasParam("SIM_PIN", true)) {
				_SIM_PIN = request->getParam("SIM_PIN", true)->value().c_str();
			}

			_useDHCP =  request->hasParam("dhcpCheckbox", true);
			if (request->hasParam("ETH_SIP", true)) {
				_Static_IP = request->getParam("ETH_SIP", true)->value().c_str();
			}
			if (request->hasParam("ETH_SM", true)) {
				_Subnet_Mask = request->getParam("ETH_SM", true)->value().c_str();
			}
			if (request->hasParam("ETH_GW", true)) {
				_Gateway_IP = request->getParam("ETH_GW", true)->value().c_str();
			}

			_useMQTT =  request->hasParam("mqttCheckbox", true);
			if (request->hasParam("mqttServer", true)) {
				_mqttServer = request->getParam("mqttServer", true)->value().c_str();
			}
			if (request->hasParam("mqttPort", true)) {
				_mqttPort = request->getParam("mqttPort", true)->value().toInt();
			}
			if (request->hasParam("mqttUser", true)) {
				_mqttUserName = request->getParam("mqttUser", true)->value().c_str();
			}
			if (request->hasParam("mqttPw", true)) {
				_mqttUserPassword = request->getParam("mqttPw", true)->value().c_str();
			}
			_useModbus = request->hasParam("modbusCheckbox", true);
			if (request->hasParam("modbusPort", true)) {
				_modbusPort = request->getParam("modbusPort", true)->value().toInt();
			}
			if (request->hasParam("modbusID", true)) {
				_modbusID = request->getParam("modbusID", true)->value().toInt();
			}
			if (request->hasParam("inputRegBase", true)) {
				_input_register_base_addr = request->getParam("inputRegBase", true)->value().toInt();
			}
			if (request->hasParam("coilBase", true)) {
				_coil_base_addr = request->getParam("coilBase", true)->value().toInt();
			}
			if (request->hasParam("discreteBase", true)) {
				_discrete_input_base_addr = request->getParam("discreteBase", true)->value().toInt();
			}
			_iotCB->onSubmitForm(request);
			saveSettings();
			SendNetworkSettings(request); });
		_pwebServer->on("/settings", HTTP_GET, [this](AsyncWebServerRequest *request)
						{ SendNetworkSettings(request); });
	}

	void IOT::SendNetworkSettings(AsyncWebServerRequest *request)
	{
		String page = network_config_top;
		page.replace("{n}", _AP_SSID);
		page.replace("{v}", CONFIG_VERSION);
		String network = network_settings_fs;
		network.replace("{AP_SSID}", _AP_SSID);
		network.replace("{AP_Pw}", _AP_Password.length() > 0 ? "******" : "");
		if (_NetworkSelection == WiFiMode)
		{
			String wifi = network_settings_wifi;
			wifi.replace("{SSID}", _AP_SSID);
			wifi.replace("{WiFi_Pw}", _AP_Password.length() > 0 ? "******" : "");
			network.replace("{NET}", wifi);
		}
		else if (_NetworkSelection == EthernetMode)
		{
			String eth;
			if (_useDHCP)
			{
				eth = network_settings_eth_dhcp;
			}
			else 
			{
				eth = network_settings_eth_st;
				eth.replace("{ETH_SIP}", _Static_IP);
				eth.replace("{ETH_SM}", _Subnet_Mask);
				eth.replace("{ETH_GW}", _Gateway_IP);
			}
			network.replace("{NET}", eth);
		}
		else if (_NetworkSelection == ModemMode)
		{
			String modem = network_settings_modem;
			modem.replace("{APN}", _APN);
			modem.replace("{SIM_USERNAME}", _SIM_Username);
			modem.replace("{SIM_PASSWORD}", _SIM_Password.length() > 0 ? "******" : "");
			modem.replace("{SIM_PIN}", _SIM_PIN);
			network.replace("{NET}", modem);
		}
		page += network;
		if (_useMQTT)
		{
			String mqtt = mqtt_settings;
			mqtt.replace("{mqttServer}", _mqttServer);
			mqtt.replace("{mqttPort}", String(_mqttPort));
			mqtt.replace("{mqttUser}", _mqttUserName);
			mqtt.replace("{mqttPw}", _mqttUserPassword.length() > 0 ? "******" : "");
			page += mqtt;
		}
		if (_useModbus)
		{
			String modbus = modbus_settings;
			modbus.replace("{modbusPort}", String(_modbusPort));
			modbus.replace("{modbusID}", String(_modbusID));
			modbus.replace("{inputRegBase}", String(_input_register_base_addr));
			modbus.replace("{coilBase}", String(_coil_base_addr));
			modbus.replace("{discreteBase}", String(_discrete_input_base_addr));
			page += modbus;
		}
		_iotCB->addApplicationSettings(page);
		page += network_settings_links;
		request->send(200, "text/html", page);
	}
	void IOT::registerMBWorkers(FunctionCode fc, MBSworker worker)
	{
		_MBserver.registerWorker(_modbusID, fc, worker);
	}

	void IOT::loadSettings()
	{
		String jsonString;
		char ch;
		for (int i = 0; i < EEPROM_SIZE; ++i)
		{
			ch = EEPROM.read(i);
			if (ch == '\0')
				break; // Stop at the null terminator
			jsonString += ch;
		}
		Serial.println(jsonString.c_str());
		JsonDocument doc;
		DeserializationError error = deserializeJson(doc, jsonString);
		if (error)
		{
			loge("Failed to load data from EEPROM, using defaults: %s", error.c_str());
			saveSettings(); // save default values
		}
		else
		{
			logd("JSON loaded from EEPROM: %d", jsonString.length());
			JsonObject iot = doc["iot"].as<JsonObject>();
			_AP_SSID = iot["AP_SSID"].isNull() ? TAG : iot["AP_SSID"].as<String>();
			_AP_Password = iot["AP_Pw"].isNull() ? DEFAULT_AP_PASSWORD : iot["AP_Pw"].as<String>();
			_NetworkSelection = iot["Network"].isNull() ? WiFiMode : iot["Network"].as<NetworkSelection>();
			_SSID = iot["SSID"].isNull() ? "" : iot["SSID"].as<String>();
			_WiFi_Password = iot["WiFi_Pw"].isNull() ? "" : iot["WiFi_Pw"].as<String>();
			_APN = iot["APN"].isNull() ? "" : iot["APN"].as<String>();
			_SIM_Username = iot["SIM_USERNAME"].isNull() ? "" : iot["SIM_USERNAME"].as<String>();
			_SIM_Password = iot["SIM_PASSWORD"].isNull() ? "" : iot["SIM_PASSWORD"].as<String>();
			_SIM_PIN = iot["SIM_PIN"].isNull() ? "" : iot["SIM_PIN"].as<String>();
			_useDHCP = iot["useDHCP"].isNull() ? false : iot["useDHCP"].as<bool>();
			_Static_IP = iot["ETH_SIP"].isNull() ? "" : iot["ETH_SIP"].as<String>();
			_Subnet_Mask = iot["ETH_SM"].isNull() ? "" : iot["ETH_SM"].as<String>();
			_Gateway_IP = iot["ETH_GW"].isNull() ? "" : iot["ETH_GW"].as<String>();

			_useMQTT = iot["useMQTT"].isNull() ? false : iot["useMQTT"].as<bool>();
			_mqttServer = iot["mqttServer"].isNull() ? "" : iot["mqttServer"].as<String>();
			_mqttPort = iot["mqttPort"].isNull() ? 1883 : iot["mqttPort"].as<uint16_t>();
			_mqttUserName = iot["mqttUser"].isNull() ? "" : iot["mqttUser"].as<String>();
			_mqttUserPassword = iot["mqttPw"].isNull() ? "" : iot["mqttPw"].as<String>();
			_useModbus = iot["useModbus"].isNull() ? false : iot["useModbus"].as<bool>();
			_modbusPort = iot["modbusPort"].isNull() ? 502 : iot["modbusPort"].as<uint16_t>();
			_modbusID = iot["modbusID"].isNull() ? 1 : iot["modbusID"].as<uint16_t>();
			_input_register_base_addr = iot["inputRegBase"].isNull() ? INPUT_REGISTER_BASE_ADDRESS : iot["inputRegBase"].as<uint16_t>();
			_coil_base_addr = iot["coilBase"].isNull() ? COIL_BASE_ADDRESS : iot["coilBase"].as<uint16_t>();
			_discrete_input_base_addr = iot["discreteBase"].isNull() ? DISCRETE_BASE_ADDRESS : iot["discreteBase"].as<uint16_t>();
			_iotCB->onLoadSetting(doc);
		}
	}

	void IOT::saveSettings()
	{
		JsonDocument doc;
		JsonObject iot = doc["iot"].to<JsonObject>();
		iot["version"] = CONFIG_VERSION;
		iot["AP_SSID"] = _AP_SSID;
		iot["AP_Pw"] = _AP_Password;
		iot["Network"] = _NetworkSelection;
		iot["SSID"] = _SSID;
		iot["WiFi_Pw"] = _WiFi_Password;
		iot["APN"] = _APN;
		iot["SIM_USERNAME"] = _SIM_Username;
		iot["SIM_PASSWORD"] = _SIM_Password;
		iot["SIM_PIN"] = _SIM_PIN;
		iot["useDHCP"] = _useDHCP;
		iot["ETH_SIP"] = _Static_IP;
		iot["ETH_SM"] = _Subnet_Mask;
		iot["ETH_GW"] = _Gateway_IP;

		iot["useMQTT"] = _useMQTT;
		iot["mqttServer"] = _mqttServer;
		iot["mqttPort"] = _mqttPort;
		iot["mqttUser"] = _mqttUserName;
		iot["mqttPw"] = _mqttUserPassword;
		iot["useModbus"] = _useModbus;
		iot["modbusPort"] = _modbusPort;
		iot["modbusID"] = _modbusID;
		iot["inputRegBase"] = _input_register_base_addr;
		iot["coilBase"] = _coil_base_addr;
		iot["discreteBase"] = _discrete_input_base_addr;
		_iotCB->onSaveSetting(doc);
		String jsonString;
		serializeJson(doc, jsonString);
		// Serial.println(jsonString.c_str());
		for (int i = 0; i < jsonString.length(); ++i)
		{
			EEPROM.write(i, jsonString[i]);
		}
		EEPROM.write(jsonString.length(), '\0'); // Null-terminate the string
		EEPROM.commit();
		logd("JSON saved, required EEPROM size: %d", jsonString.length());
	}

	void IOT::Run()
	{
		uint32_t now = millis();
		if (_networkState == Boot && _NetworkSelection == NotConnected)
		{ // Network not setup?, see if flasher is trying to send us the SSID/Pw
			if (Serial.peek() == '{')
			{
				String s = Serial.readStringUntil('}');
				s += "}";
				JsonDocument doc;
				DeserializationError err = deserializeJson(doc, s);
				if (err)
				{
					loge("deserializeJson() failed: %s", err.c_str());
				}
				else
				{
					if (doc["ssid"].is<const char *>() && doc["password"].is<const char *>())
					{
						_SSID = doc["ssid"].as<String>();
						logd("Setting ssid: %s", _SSID.c_str());
						_WiFi_Password = doc["password"].as<String>();
						logd("Setting password: %s", _WiFi_Password.c_str());
						_NetworkSelection = WiFiMode;
						saveSettings();
						esp_restart();
					}
					else
					{
						logw("Received invalid json: %s", s.c_str());
					}
				}
			}
			else
			{
				Serial.read(); // discard data
			}
			if ((now - _FlasherIPConfigStart) > FLASHER_TIMEOUT) // wait for flasher tool to send Wifi info
			{ 
				logd("Done waiting for flasher!");
				setState(ApState); // switch to AP mode for AP_TIMEOUT
			}
		}
		else if (_networkState == Boot) // have network selection, start with wifiAP for AP_TIMEOUT then STA mode
		{
			setState(ApState); // switch to AP mode for AP_TIMEOUT
		}
		else if (_networkState == ApState)
		{
			if ((now - _waitInAPTimeStamp) > AP_TIMEOUT) // switch to selected network after waiting in APMode for AP_TIMEOUT duration
			{ 
				logd("Connecting to network: %d", _NetworkSelection);
				setState(Connecting);
			}
			_dnsServer.processNextRequest();
			_webLog.process();
		}
		else if (_networkState == Connecting)
		{
			if ((millis() - _NetworkConnectionStart) > WIFI_CONNECTION_TIMEOUT)
			{
				// -- Network not available, fall back to AP mode.
				logw("Giving up on Network connection.");
				WiFi.disconnect(true);
				setState(ApState);
			}
		}
		else if (_networkState == OffLine) // went offline, try again...
		{
			setState(Connecting);
		}
		else if (_networkState == OnLine)
		{
			_webLog.process();
		}
#ifndef LOG_TO_SERIAL_PORT
		// use LED if the log level is none (edgeBox shares the LED pin with the serial TX gpio)
		// handle blink led, fast : NotConnected slow: AP connected On: Station connected
		if (_networkState != OnLine)
		{
			unsigned long binkRate = _networkState == ApState ? AP_BLINK_RATE : NC_BLINK_RATE;
			unsigned long now = millis();
			if (binkRate < now - _lastBlinkTime)
			{
				_blinkStateOn = !_blinkStateOn;
				_lastBlinkTime = now;
				digitalWrite(WIFI_STATUS_PIN, _blinkStateOn ? HIGH : LOW);
			}
		}
		else
		{
			digitalWrite(WIFI_STATUS_PIN, HIGH);
		}
#endif
		vTaskDelay(pdMS_TO_TICKS(20));
		return;
	}

	void IOT::GoOnline()
	{
		logd("GoOnline called");
		_pwebServer->begin();
		_webLog.begin(_pwebServer);
		_OTA.begin(_pwebServer);
		if (_networkState > APMode)
		{
			if (_NetworkSelection == EthernetMode || _NetworkSelection == WiFiMode)
			{
				MDNS.begin(_AP_SSID.c_str());
				MDNS.addService("http", "tcp", ASYNC_WEBSERVER_PORT);
				logd("Active mDNS services: %d", MDNS.queryService("http", "tcp"));
			}
			this->IOTCB()->onNetworkConnect();
			if (_useModbus && !_MBserver.isRunning())
			{
				_MBserver.start(_modbusPort, 5, 0); // listen for modbus requests
			}
			logd("Before xTimerStart _NetworkSelection: %d", _NetworkSelection );
			xTimerStart(mqttReconnectTimer, 0);
			setState(OnLine);
		}
	}

	void IOT::GoOffline()
	{
		xTimerStop(mqttReconnectTimer, 0); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
		_webLog.end();
		_dnsServer.stop();
		MDNS.end();
		setState(OffLine);
	}

	void IOT::setState(NetworkState newState)
	{
		NetworkState oldState = _networkState;
		_networkState = newState;
		switch (newState)
		{
		case OffLine:
			WiFi.disconnect(true);
			WiFi.mode(WIFI_OFF);
			DisconnectModem();
			DisconnectEthernet();
			break;
		case ApState:
			if ((oldState == Connecting) || (oldState == OnLine))
			{
				WiFi.disconnect(true);
				DisconnectModem();
				DisconnectEthernet();
			}
			WiFi.mode(WIFI_AP);
			if (WiFi.softAP(_AP_SSID, _AP_Password))
			{
				IPAddress IP = WiFi.softAPIP();
				logi("WiFi AP SSID: %s PW: %s", _AP_SSID.c_str(), _AP_Password.c_str());
				logd("AP IP address: %s", IP.toString().c_str());
				_dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
				_dnsServer.start(DNS_PORT, "*", IP);
			}
			_waitInAPTimeStamp = millis();
			break;
		case Connecting:
			_NetworkConnectionStart = millis();
			if (_NetworkSelection == WiFiMode)
			{			
				WiFi.setHostname(_AP_SSID.c_str());
				WiFi.mode(WIFI_STA);
				WiFi.begin(_SSID, _WiFi_Password);
			} 
			else if (_NetworkSelection == EthernetMode)
			{
				if (ConnectEthernet() == ESP_OK)
				{
					logd("Ethernet succeeded");
				}
				else
				{
					loge("Failed to connect to Ethernet");
				}				
			}
			else if (_NetworkSelection == ModemMode)
			{
				if (ConnectModem() == ESP_OK)
				{
					logd("Modem succeeded");
				}
				else
				{
					loge("Failed to connect to 4G Modem");
				}
			}
			break;
		case OnLine:
			logd("State: Online");
			break;
		default:
			break;
		}
	}

	void IOT::HandleMQTT(int32_t event_id, void *event_data)
	{
		auto event = (esp_mqtt_event_handle_t)event_data;
		esp_mqtt_client_handle_t client = event->client;
		int msg_id;
		JsonDocument doc;
		switch ((esp_mqtt_event_id_t)event_id)
		{
		case MQTT_EVENT_CONNECTED:
			logi("Connected to MQTT.");
			char buf[128];
			sprintf(buf, "%s/cmnd/#", _rootTopicPrefix);
			esp_mqtt_client_subscribe(client, buf, 0);
			IOTCB()->onMqttConnect();
			msg_id = esp_mqtt_client_publish(client, _willTopic, "Offline", 0, 1, 0);
			break;
		case MQTT_EVENT_DISCONNECTED:
			logw("Disconnected from MQTT");
			if (_networkState == OnLine)
			{
				xTimerStart(mqttReconnectTimer, 5000);
			}
			break;

		case MQTT_EVENT_SUBSCRIBED:
			logi("MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
			break;
		case MQTT_EVENT_UNSUBSCRIBED:
			logi("MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
			break;
		case MQTT_EVENT_PUBLISHED:
			logi("MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
			break;
		case MQTT_EVENT_DATA:
			logd("MQTT Message arrived [%s]  qos: %d len: %d index: %d total: %d", event->topic, event->qos, event->data_len, event->current_data_offset, event->total_data_len);
			if (deserializeJson(doc, event->data)) // not json!
			{
				logd("MQTT payload {%s} is not valid JSON!", event->data);
			}
			else
			{
				if (doc.containsKey("status"))
				{
					doc.clear();
					doc["sw_version"] = CONFIG_VERSION;
					// doc["IP"] = WiFi.localIP().toString().c_str();
					// doc["SSID"] = WiFi.SSID();
					doc["uptime"] = formatDuration(millis() - _lastBootTimeStamp);
					Publish("status", doc, true);
				}
				else
				{
					IOTCB()->onMqttMessage(event->topic, doc);
				}
			}
			break;
		case MQTT_EVENT_ERROR:
			loge("MQTT_EVENT_ERROR");
			if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
			{
				logi("Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
			}
			break;
		default:
			logi("Other event id:%d", event->event_id);
			break;
		}
	}

	void IOT::ConnectToMQTTServer()
	{
		if (_networkState == OnLine)
		{
			if (_useMQTT && _mqttServer.length() > 0) // mqtt configured?
			{
				logd("Connecting to MQTT...");
				int len = strlen(_AP_SSID.c_str());
				strncpy(_rootTopicPrefix, _AP_SSID.c_str(), len);
				logd("rootTopicPrefix: %s", _rootTopicPrefix);
				sprintf(_willTopic, "%s/tele/LWT", _rootTopicPrefix);
				logd("_willTopic: %s", _willTopic);
				esp_mqtt_client_config_t mqtt_cfg = {};
				mqtt_cfg.host = _mqttServer.c_str();
				mqtt_cfg.port = _mqttPort;
				mqtt_cfg.username = _mqttUserName.c_str();
				mqtt_cfg.password = _mqttUserPassword.c_str();
				mqtt_cfg.client_id = _AP_SSID.c_str();
				mqtt_cfg.lwt_topic = _willTopic;
				mqtt_cfg.lwt_retain = 1;
				mqtt_cfg.lwt_msg = "Offline";
				mqtt_cfg.lwt_msg_len = 7;
				_mqtt_client_handle = esp_mqtt_client_init(&mqtt_cfg);
				esp_mqtt_client_register_event(_mqtt_client_handle, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, mqtt_event_handler, this);
				esp_mqtt_client_start(_mqtt_client_handle);
			}
		}
	}

	boolean IOT::Publish(const char *subtopic, JsonDocument &payload, boolean retained)
	{
		String s;
		serializeJson(payload, s);
		return Publish(subtopic, s.c_str(), retained);
	}

	boolean IOT::Publish(const char *subtopic, const char *value, boolean retained)
	{
		boolean rVal = false;
		if (_mqtt_client_handle != 0)
		{
			char buf[128];
			sprintf(buf, "%s/stat/%s", _rootTopicPrefix, subtopic);
			rVal = (esp_mqtt_client_publish(_mqtt_client_handle, buf, value, strlen(value), 1, retained) != -1);
			if (!rVal)
			{
				loge("**** Failed to publish MQTT message");
			}
		}
		return rVal;
	}

	boolean IOT::Publish(const char *topic, float value, boolean retained)
	{
		char buf[256];
		snprintf_P(buf, sizeof(buf), "%.1f", value);
		return Publish(topic, buf, retained);
	}

	boolean IOT::PublishMessage(const char *topic, JsonDocument &payload, boolean retained)
	{
		boolean rVal = false;
		if (_mqtt_client_handle != 0)
		{
			String s;
			serializeJson(payload, s);
			rVal = (esp_mqtt_client_publish(_mqtt_client_handle, topic, s.c_str(), s.length(), 0, retained) != -1);
			if (!rVal)
			{
				loge("**** Configuration payload exceeds MAX MQTT Packet Size, %d [%s] topic: %s", s.length(), s.c_str(), topic);
			}
		}
		return rVal;
	}

	boolean IOT::PublishHADiscovery(JsonDocument &payload)
	{
		boolean rVal = false;
		if (_mqtt_client_handle != 0)
		{
			char topic[64];
			sprintf(topic, "%s/device/%s_%X/config", HOME_ASSISTANT_PREFIX, TAG, getUniqueId());
			rVal = PublishMessage(topic, payload, true);
		}
		return rVal;
	}

	std::string IOT::getRootTopicPrefix()
	{
		std::string s(_rootTopicPrefix);
		return s;
	};

	std::string IOT::getThingName()
	{
		std::string s(_AP_SSID.c_str());
		return s;
	}

	void IOT::PublishOnline()
	{
		if (!_publishedOnline)
		{
			if (esp_mqtt_client_publish(_mqtt_client_handle, _willTopic, "Online", 0, 1, 1) != -1)
			{
				_publishedOnline = true;
			}
		}
	}

	void IOT::HandleIPEvent(int32_t event_id, void *event_data)
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
			GoOnline();
		}
		else if (event_id == IP_EVENT_PPP_LOST_IP)
		{
			logi("Modem Disconnect from PPP Server");
			GoOffline();
		}
		else if (event_id == IP_EVENT_ETH_LOST_IP)
		{
			logi("Ethernet Disconnect");
			GoOffline();
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

	esp_err_t IOT::ConnectEthernet()
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
				.address_bits = 8,	// Actually it's the control phase in W5500 SPI frame
				.mode = 0,
				.clock_speed_hz = 25 * 1000 * 1000,
				.spics_io_num = SS,
				.queue_size = 20,
			};
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
			_eth_handle = NULL;
			esp_eth_config_t eth_config_spi = ETH_DEFAULT_CONFIG(mac, phy);
			if ((ret = esp_eth_driver_install(&eth_config_spi, &_eth_handle)) != ESP_OK)
			{
				loge("esp_eth_driver_install failed");
				return ret;
			}
			if ((ret = esp_eth_ioctl(_eth_handle, ETH_CMD_S_MAC_ADDR, local_mac_1)) != ESP_OK) // set mac address
			{
				logd("SPI Ethernet MAC address config failed");
			}
			esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH(); // Initialize the Ethernet interface
			_netif = esp_netif_new(&cfg);
			assert(_netif);
			if (!_useDHCP)
			{
				esp_netif_dhcpc_stop(_netif);
				esp_netif_ip_info_t ipInfo;
				IPAddress ip;
				ip.fromString(_Static_IP);
				ipInfo.ip.addr =  static_cast<uint32_t>(ip);
				ip.fromString(_Subnet_Mask);
				ipInfo.netmask.addr = static_cast<uint32_t>(ip);
				ip.fromString(_Gateway_IP);
				ipInfo.gw.addr = static_cast<uint32_t>(ip);
				if ((ret = esp_netif_set_ip_info(_netif, &ipInfo)) != ESP_OK)
				{
					loge("esp_netif_set_ip_info failed: %d", ret);
					return ret;
				}
			}
			eth_netif_glue = esp_eth_new_netif_glue(_eth_handle);
			if ((ret = esp_netif_attach(_netif, eth_netif_glue)) != ESP_OK)
			{
				loge("esp_netif_attach failed");
				return ret;
			}
			if ((ret = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &on_ip_event, this)) != ESP_OK)
			{
				loge("esp_event_handler_register IP_EVENT->IP_EVENT_ETH_GOT_IP failed");
				return ret;
			}
			if ((ret = esp_eth_start(_eth_handle)) != ESP_OK)
			{
				loge("esp_netif_attach failed");
				return ret;
			}
		}
		return ret;
	}

	void IOT::wakeup_modem(void)
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

	esp_err_t IOT::ConnectModem()
	{
		logd("ConnectModem");
		esp_err_t ret = ESP_OK;
		wakeup_modem();
		esp_netif_config_t ppp_netif_config = ESP_NETIF_DEFAULT_PPP(); // Initialize lwip network interface in PPP mode
		_netif = esp_netif_new(&ppp_netif_config);
		assert(_netif);
		ESP_ERROR_CHECK(modem_init_network(_netif, _APN.c_str(), _SIM_PIN.c_str())); // Initialize the PPP network and register for IP event
		if (_SIM_Username.length() > 0)
		{
			esp_netif_ppp_set_auth(_netif, NETIF_PPP_AUTHTYPE_PAP, _SIM_Username.c_str(), _SIM_Password.c_str());
		}
		ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, on_ip_event, this));
		int retryCount = 3;
		while (retryCount-- != 0)
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
		logi("Modem has acquired network");
		return ret;
	}

	void IOT::DisconnectModem()
	{
		modem_stop_network();
		modem_deinit_network();
		esp_netif_destroy(_netif);
		ESP_ERROR_CHECK(esp_netif_deinit());
		ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, ESP_EVENT_ANY_ID, on_ip_event));
	}

	void IOT::DisconnectEthernet()
	{
        ESP_ERROR_CHECK(esp_eth_stop(_eth_handle));
        ESP_ERROR_CHECK(esp_eth_del_netif_glue(eth_netif_glue));
		esp_netif_destroy(_netif);
		ESP_ERROR_CHECK(esp_netif_deinit());
		ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, ESP_EVENT_ANY_ID, on_ip_event));
	}

} // namespace EDGEBOX