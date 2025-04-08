
#include <sys/time.h>
#include <thread>
#include <chrono>
#include <ESPmDNS.h>
#include <SPI.h>
#include <Ethernet.h>
#include <esp_netif.h>
#include <esp_eth.h>
#include <esp_event.h>
#include "Log.h"
#include "WebLog.h"
#include "IOT.h"
#include "IOT.html"
#include "HelperFunctions.h"
#include "EdgeBoxNet.h"

namespace EDGEBOX
{
	EdgeBoxNet _network;

	TimerHandle_t mqttReconnectTimer;
	static DNSServer _dnsServer;
	static WebLog _webLog;
	static ModbusServerTCPasync _MBserver;
	static AsyncAuthenticationMiddleware basicAuth;

	void IOT::GoOnline()
	{
		logd("GoOnline called");
		_pwebServer->begin();
		_webLog.begin(_pwebServer);
		_OTA.begin(_pwebServer);
		if (_NetworkStatus != APMode)
		{
			// MDNS.begin(_AP_SSID.c_str());
			// MDNS.addService("http", "tcp", ASYNC_WEBSERVER_PORT);
			// logd("Active mDNS services: %d", MDNS.queryService("http", "tcp"));
			// xTimerStart(mqttReconnectTimer, 0);
			this->IOTCB()->onWiFiConnect();
			if (_useModbus && !_MBserver.isRunning())
			{
				_MBserver.start(_modbusPort, 5, 0); // listen for modbus requests
			}
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

	void IOT::Init(IOTCallbackInterface *iotCB, AsyncWebServer *pwebServer)
	{
		_iotCB = iotCB;
		_pwebServer = pwebServer;
		pinMode(FACTORY_RESET_PIN, INPUT_PULLUP);
#ifdef WIFI_STATUS_PIN
		pinMode(WIFI_STATUS_PIN, OUTPUT);
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
				_NetworkStatus = NotConnected;
				GoOffline();
			break;
			case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
				logd("AP_STAIPASSIGNED");
				_NetworkStatus = APMode;
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
				_NetworkStatus = WiFiMode;
				GoOnline();
				break;
			case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
				logw("STA_DISCONNECTED");
				_NetworkStatus = NotConnected;
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
			fields.replace("{SSID}", _SSID);
			fields.replace("{WiFi_Pw}", _WiFi_Password);
			fields.replace("{mqttchecked}", _useMQTT ? "checked" : "unchecked");
			fields.replace("{mqttServer}", _mqttServer);
			fields.replace("{mqttPort}", String(_mqttPort));
			fields.replace("{mqttUser}", _mqttUserName);
			fields.replace("{mqttPw}", _mqttUserPassword);
			fields.replace("{modbuschecked}", _useModbus ? "checked" : "unchecked");
			fields.replace("{modbusPort}", String(_modbusPort));
			fields.replace("{modbusID}", String(_modbusID));
			Serial.println(fields.c_str());
			String page = network_config_top;
			page.replace("{n}", _AP_SSID);
			page.replace("{v}", CONFIG_VERSION);
			page += fields;
			_iotCB->addNetworkConfigs(page);
			String apply_button = network_config_apply_button;
			page += apply_button;
			page += network_config_links;
			request->send(200, "text/html", page); })
			.addMiddleware(&basicAuth);

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
			if (request->hasParam("WiFi_Pw", true)) {
				_WiFi_Password = request->getParam("WiFi_Pw", true)->value().c_str();
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
		String network = network_settings;
		network.replace("{AP_SSID}", _AP_SSID);
		network.replace("{AP_Pw}", _AP_Password.length() > 0 ? "******" : "");
		network.replace("{SSID}", _SSID);
		network.replace("{WiFi_Pw}", _WiFi_Password.length() > 0 ? "******" : "");
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
			page += modbus;
		}
		_iotCB->addNetworkSettings(page);
		page += network_settings_links;
		request->send(200, "text/html", page);
	}
	void IOT::registerMBWorkers(FunctionCode fc, MBSworker worker)
	{
		_MBserver.registerWorker(_modbusID, fc, worker);
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

	void IOT::Run()
	{
		uint32_t now = millis();
		if (_networkState == Boot && _SSID.length() == 0)
		{ // WiFi not setup, see if flasher is trying to send us the SSID/Pw
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
			setState(ConnectingEthernet); // try ethernet
		}
		else if (_networkState == Boot) // have SSID, start with wifiAP for AP_TIMEOUT then STA mode
		{
			setState(ApMode); // switch to AP mode for AP_TIMEOUT
		}
		else if (_networkState == ApMode)
		{
			if (_NetworkStatus == NotConnected && _SSID.length() > 0) // wifi configured and no AP connections
			{
				if (now > (_waitInAPTimeStamp + AP_TIMEOUT))
				{ // switch to WiFi after waiting for AP connection
					logd("Connecting to WiFi : %s", _SSID.c_str());
					setState(ConnectingWifi);
				}
			}
			_dnsServer.processNextRequest();
			_webLog.process();
		}
		else if (_networkState == ConnectingWifi)
		{
			if (WiFi.status() != WL_CONNECTED)
			{
				if ((millis() - _wifiConnectionStart) > WIFI_CONNECTION_TIMEOUT)
				{
					// -- WiFi not available, fall back to AP mode.
					logw("Giving up on WiFi connection.");
					WiFi.disconnect(true);
					setState(ApMode);
				}
			}
			else
			{
				logi("WiFi connected, IP address: %s", WiFi.localIP().toString().c_str());
				setState(OnLine);
				return;
			}
		}
		else if (_networkState == OnLine)
		{
			_webLog.process();
		}
#ifdef WIFI_STATUS_PIN
		// handle blink led, fast : NotConnected slow: AP connected On: Station connected
		if (_NetworkStatus != WiFiMode)
		{
			unsigned long binkRate = _NetworkStatus == NotConnected ? NC_BLINK_RATE : AP_BLINK_RATE;
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
		return;
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
			break;
		case ApMode:
			if ((oldState == ConnectingWifi) || (oldState == OnLine))
			{
				WiFi.disconnect(true);
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
		case ConnectingWifi:
			_wifiConnectionStart = millis();
			WiFi.setHostname(_AP_SSID.c_str());
			WiFi.mode(WIFI_STA);
			WiFi.begin(_SSID, _WiFi_Password);
			break;

		case ConnectingEthernet:
			logd("SetupEthernet");
			if (_network.ConnectEthernet(this) == ESP_OK)
			{
				logd("Ethernet succeeded");
			}
			else
			{
				loge("Failed to connect to Ethernet");
			}
			break;

		case ConnectingModem:
			logd("SetupEthernet");
			if (_network.ConnectModem(this) == ESP_OK)
			{
				logd("Modem succeeded");
			}
			else
			{
				loge("Failed to connect to 4G Modem");
			}
			break;
		case OnLine:
			logd("State: Online");
			break;
		default:
			break;
		}
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
		// Serial.println(jsonString.c_str());
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
			_SSID = iot["SSID"].isNull() ? "" : iot["SSID"].as<String>();
			_WiFi_Password = iot["WiFi_Pw"].isNull() ? "" : iot["WiFi_Pw"].as<String>();
			_useMQTT = iot["useMQTT"].isNull() ? false : iot["useMQTT"].as<bool>();
			_mqttServer = iot["mqttServer"].isNull() ? "" : iot["mqttServer"].as<String>();
			_mqttPort = iot["mqttPort"].isNull() ? 1883 : iot["mqttPort"].as<uint16_t>();
			_mqttUserName = iot["mqttUser"].isNull() ? "" : iot["mqttUser"].as<String>();
			_mqttUserPassword = iot["mqttPw"].isNull() ? "" : iot["mqttPw"].as<String>();
			_useModbus = iot["useModbus"].isNull() ? false : iot["useModbus"].as<bool>();
			_modbusPort = iot["modbusPort"].isNull() ? 502 : iot["modbusPort"].as<uint16_t>();
			_modbusID = iot["modbusID"].isNull() ? 1 : iot["modbusID"].as<uint16_t>();

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
		iot["SSID"] = _SSID;
		iot["WiFi_Pw"] = _WiFi_Password;
		iot["useMQTT"] = _useMQTT;
		iot["mqttServer"] = _mqttServer;
		iot["mqttPort"] = _mqttPort;
		iot["mqttUser"] = _mqttUserName;
		iot["mqttPw"] = _mqttUserPassword;
		iot["useModbus"] = _useModbus;
		iot["modbusPort"] = _modbusPort;
		iot["modbusID"] = _modbusID;
		_iotCB->onSaveSetting(doc);
		String jsonString;
		serializeJson(doc, jsonString);
		// Serial.println(jsonString.c_str());
		logd("_useMQTT: %d", _useMQTT);
		for (int i = 0; i < jsonString.length(); ++i)
		{
			EEPROM.write(i, jsonString[i]);
		}
		EEPROM.write(jsonString.length(), '\0'); // Null-terminate the string
		EEPROM.commit();
		logd("JSON saved, required EEPROM size: %d", jsonString.length());
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
			rVal = (esp_mqtt_client_publish(_mqtt_client_handle, topic,  s.c_str(), s.length(), 0, retained) != -1);
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
			sprintf(topic, "%s/device/%X/config", HOME_ASSISTANT_PREFIX, getUniqueId());
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

} // namespace EDGEBOX