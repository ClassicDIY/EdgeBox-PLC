#include <Arduino.h>
#include "Log.h"
#include "IOT.h"
#include "PLC.h"
#include "PLC.html"

namespace EDGEBOX
{
	static AsyncWebServer _asyncServer(ASYNC_WEBSERVER_PORT);
	static AsyncWebSocket _webSocket("/ws_home");
	IOT _iot = IOT();

	void PLC::addApplicationSettings(String &page)
	{
		String appFields = app_settings_fields;
		appFields.replace("{digitalInputs}", String(_digitalInputs));
		appFields.replace("{analogInputs}", String(_analogInputs));

		String appConvs;
		for (int i = 0; i < _analogInputs; i++)
		{
			String conv_flds(analog_conv_val);
			conv_flds.replace("{An}", "A" + String(i));
			conv_flds.replace("{minV}", String(_AnalogSensors[i].minV(), 1));
			conv_flds.replace("{minT}", String(_AnalogSensors[i].minT(), 1));
			conv_flds.replace("{maxV}", String(_AnalogSensors[i].maxV(), 1));
			conv_flds.replace("{maxT}", String(_AnalogSensors[i].maxT(), 1));
			appConvs += conv_flds;	
		}
		appFields.replace("{aconv}", appConvs);
		page += appFields;
	}

	void PLC::addApplicationConfigs(String &page)
	{
		String appFields = app_config_fields;
		
		appFields.replace("{digitalInputs}", String(_digitalInputs));
		appFields.replace("{analogInputs}", String(_analogInputs));
		String appConvs;
		String scriptConvs;
		for (int i = 0; i < _analogInputs; i++)
		{
			String conv_flds(analog_conv_flds);
			String conv_script(app_validateInputs);
			conv_flds.replace("{An}", "A" + String(i));
			conv_script.replace("{An}", "A" + String(i));
			conv_flds.replace("{minV}", String(_AnalogSensors[i].minV(), 1));
			conv_flds.replace("{minT}", String(_AnalogSensors[i].minT(), 1));
			conv_flds.replace("{maxV}", String(_AnalogSensors[i].maxV(), 1));
			conv_flds.replace("{maxT}", String(_AnalogSensors[i].maxT(), 1));
			scriptConvs += conv_script;
			appConvs += conv_flds;	
		}
		appFields.replace("{aconv}", appConvs);
		page += appFields;
		page.replace("{validateInputs}", scriptConvs);
	}

	void PLC::onSubmitForm(AsyncWebServerRequest *request)
	{
		if (request->hasParam("digitalInputs", true))
		{
			_digitalInputs = request->getParam("digitalInputs", true)->value().toInt();
		}
		if (request->hasParam("analogInputs", true))
		{
			_analogInputs = request->getParam("analogInputs", true)->value().toInt();
		}
		for (int i = 0; i < _analogInputs; i++)
		{
			String ain = "A" + String(i);
			if (request->hasParam(ain + "_min", true))
			{
				_AnalogSensors[i].SetMinV(request->getParam(ain + "_min", true)->value().toFloat());
			}
			if (request->hasParam(ain + "_min_t", true))
			{
				_AnalogSensors[i].SetMinT(request->getParam(ain + "_min_t", true)->value().toFloat());
			}	
			if (request->hasParam(ain + "_max", true))
			{
				_AnalogSensors[i].SetMaxV(request->getParam(ain + "_max", true)->value().toFloat());
			}
			if (request->hasParam(ain + "_max_t", true))
			{
				_AnalogSensors[i].SetMaxT(request->getParam(ain + "_max_t", true)->value().toFloat());
			}		
		}
	}

	void PLC::onSaveSetting(JsonDocument &doc)
	{
		JsonObject plc = doc["plc"].to<JsonObject>();
		plc["digitalInputs"] = _digitalInputs;
		plc["analogInputs"] = _analogInputs;
		for (int i = 0; i < _analogInputs; i++)
		{
			String ain = "A" + String(i);
			plc[ain + "_minV"] = _AnalogSensors[i].minV();
			plc[ain + "_minT"] = _AnalogSensors[i].minT();
			plc[ain + "_maxV"] = _AnalogSensors[i].maxV();
			plc[ain + "_maxT"] = _AnalogSensors[i].maxT();
		}
	}

	void PLC::onLoadSetting(JsonDocument &doc)
	{
		JsonObject plc = doc["plc"].as<JsonObject>();
		_digitalInputs = plc["digitalInputs"].isNull() ? DI_PINS : plc["digitalInputs"].as<uint16_t>();
		_analogInputs = plc["analogInputs"].isNull() ? AI_PINS : plc["analogInputs"].as<uint16_t>();
		for (int i = 0; i < _analogInputs; i++)
		{
			String ain = "A" + String(i);
			plc[ain + "_minV"].isNull() ? _AnalogSensors[i].SetMinV(1.0) : _AnalogSensors[i].SetMinV(plc[ain + "_minV"].as<float>());
			plc[ain + "_minT"].isNull() ? _AnalogSensors[i].SetMinT(0.0) : _AnalogSensors[i].SetMinT(plc[ain + "_minT"].as<float>());
			plc[ain + "_maxV"].isNull() ? _AnalogSensors[i].SetMaxV(5.0) : _AnalogSensors[i].SetMaxV(plc[ain + "_maxV"].as<float>());
			plc[ain + "_maxT"].isNull() ? _AnalogSensors[i].SetMaxT(100.0) : _AnalogSensors[i].SetMaxT(plc[ain + "_maxT"].as<float>());
		}
	}

	void PLC::setup()
	{
		logd("setup");
		_iot.Init(this, &_asyncServer);
		_asyncServer.on("/", HTTP_GET, [this](AsyncWebServerRequest *request)
						{
			String page = home_html;
			page.replace("{n}", _iot.getThingName().c_str());
			page.replace("{v}", CONFIG_VERSION);
			String s;
			for (int i = 0; i < _digitalInputs; i++)
			{
				s += "<div class='box' id=";
				s += _DigitalSensors[i].Pin().c_str();
				s += "> ";
				s += _DigitalSensors[i].Pin().c_str();
				s += "</div>";
			}
			page.replace("{digitalInputs}", s);
			s.clear();
			for (int i = 0; i < _analogInputs; i++)
			{
				s += "<div class='box' id=";
				s += _AnalogSensors[i].Channel().c_str();
				s += "> ";
				s += _AnalogSensors[i].Channel().c_str();
				s += "</div>";
			}
			page.replace("{analogInputs}", s);
			s.clear();
			for (int i = 0; i < DO_PINS; i++)
			{
				s += "<div class='box' id=";
				s += _Coils[i].Pin().c_str();
				s += "> ";
				s += _Coils[i].Pin().c_str();
				s += "</div>";
			}
			page.replace("{digitalOutputs}", s);
			request->send(200, "text/html", page); });
		_asyncServer.addHandler(&_webSocket).addMiddleware([this](AsyncWebServerRequest *request, ArMiddlewareNext next)
														   {
			// ws.count() is the current count of WS clients: this one is trying to upgrade its HTTP connection
			if (_webSocket.count() > 1) {
			// if we have 2 clients or more, prevent the next one to connect
			request->send(503, "text/plain", "Server is busy");
			} else {
			// process next middleware and at the end the handler
			next();
		} });
		_webSocket.onEvent([this](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
						   {
			(void)len;
			if (type == WS_EVT_CONNECT) {
				_lastMessagePublished.clear(); //force a broadcast
				client->setCloseClientOnQueueFull(false);
				client->ping();
			} else if (type == WS_EVT_DISCONNECT) {
				// logi("Home Page Disconnected!");
			} else if (type == WS_EVT_ERROR) {
				loge("ws error");
			// } else if (type == WS_EVT_PONG) {
            // 	logd("ws pong");
        	} });
	}

	void PLC::onNetworkConnect()
	{
		// READ_INPUT_REGISTER
		auto modbusFC04 = [this](ModbusMessage request) -> ModbusMessage
		{
			ModbusMessage response;
			uint16_t addr = 0;
			uint16_t words = 0;
			request.get(2, addr);
			request.get(4, words);
			// logd("READ_INPUT_REGISTER %d %d[%d]", request.getFunctionCode(), addr, words);
			addr -= _iot.InputRegisterBaseAddr();
			if ((addr + words) > AI_PINS)
			{
				logw("READ_INPUT_REGISTER error: %d", (addr + words));
				response.setError(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
			}
			else
			{
				response.add(request.getServerID(), request.getFunctionCode(), (uint8_t)(words * 2));
				for (int i = addr; i < (addr + words); i++)
				{
					response.add((uint16_t)_AnalogSensors[i].Level());
				}
			}
			return response;
		};

		// READ_COIL
		auto modbusFC01 = [this](ModbusMessage request) -> ModbusMessage
		{
			ModbusMessage response; // The Modbus message we are going to give back
			uint16_t start = 0;
			uint16_t numCoils = 0;
			request.get(2, start, numCoils);
			logd("READ_COIL %d %d[%d]", request.getFunctionCode(), start, numCoils);
			// Address overflow?
			start -= _iot.CoilBaseAddr();
			if ((start + numCoils) > DO_PINS)
			{
				logw("READ_COIL error: %d", (start + numCoils));
				response.setError(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
			}
			for (int i = 0; i < DO_PINS; i++)
			{
				_digitalOutputCoils.set(i, _Coils[i].Level());
			}
			vector<uint8_t> coilset = _digitalOutputCoils.slice(start, numCoils);
			response.add(request.getServerID(), request.getFunctionCode(), (uint8_t)coilset.size(), coilset);
			return response;
		};

		// READ_DISCR_INPUT
		auto modbusFC02 = [this](ModbusMessage request) -> ModbusMessage
		{
			ModbusMessage response; // The Modbus message we are going to give back
			uint16_t start = 0;
			uint16_t numDiscretes = 0;
			request.get(2, start, numDiscretes);
			logd("READ_DISCR_INPUT %d %d[%d]", request.getFunctionCode(), start, numDiscretes);
			start -= _iot.DiscreteBaseAddr();
			// Address overflow?
			if ((start + numDiscretes) > DI_PINS)
			{
				logw("READ_DISCR_INPUT error: %d", (start + numDiscretes));
				response.setError(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
			}
			for (int i = 0; i < DI_PINS; i++)
			{
				_digitalInputDiscretes.set(i, _DigitalSensors[i].Level());
			}
			vector<uint8_t> coilset = _digitalInputDiscretes.slice(start, numDiscretes);
			response.add(request.getServerID(), request.getFunctionCode(), (uint8_t)coilset.size(), coilset);
			return response;
		};

		// WRITE_COIL
		auto modbusFC05 = [this](ModbusMessage request) -> ModbusMessage
		{
			ModbusMessage response;
			// Request parameters are coil number and 0x0000 (OFF) or 0xFF00 (ON)
			uint16_t start = 0;
			uint16_t state = 0;
			request.get(2, start, state);
			logd("WRITE_COIL %d %d:%d", request.getFunctionCode(), start, state);
			start -= _iot.CoilBaseAddr();
			// Is the coil number within the range of the coils?
			if (start <= DO_PINS)
			{
				// Looks like it. Is the ON/OFF parameter correct?
				if (state == 0x0000 || state == 0xFF00)
				{
					// Yes. We can set the coil
					if (_digitalOutputCoils.set(start, state))
					{
						_Coils[start].Set(state == 0xFF00 ? HIGH : LOW);
						// All fine, coil was set.
						response = ECHO_RESPONSE;
					}
					else
					{
						// Setting the coil failed
						response.setError(request.getServerID(), request.getFunctionCode(), SERVER_DEVICE_FAILURE);
					}
				}
				else
				{
					// Wrong data parameter
					response.setError(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_VALUE);
				}
			}
			else
			{
				// Something was wrong with the coil number
				response.setError(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
			}
			// Return the response
			return response;
		};

		// WRITE_MULT_COILS
		auto modbusFC0F = [this](ModbusMessage request) -> ModbusMessage
		{
			ModbusMessage response;
			// Request parameters are first coil to be set, number of coils, number of bytes and packed coil bytes
			uint16_t start = 0;
			uint16_t numCoils = 0;
			uint8_t numBytes = 0;
			uint16_t offset = 2; // Parameters start after serverID and FC
			offset = request.get(offset, start, numCoils, numBytes);
			logd("WRITE_MULT_COILS %d %d[%d]", request.getFunctionCode(), start, numCoils);
			start -= _iot.CoilBaseAddr();
			// Check the parameters so far
			if (start + numCoils <= DO_PINS)
			{
				// Packed coils will fit in our storage
				if (numBytes == ((numCoils - 1) >> 3) + 1)
				{
					// Byte count seems okay, so get the packed coil bytes now
					vector<uint8_t> coilset;
					request.get(offset, coilset, numBytes);
					// Now set the coils
					if (_digitalOutputCoils.set(start, numCoils, coilset))
					{
						for (int i = 0; i < DO_PINS; i++)
						{
							_Coils[i].Set(_digitalOutputCoils[i]);
						}
						// All fine, return shortened echo response, like the standard says
						response.add(request.getServerID(), request.getFunctionCode(), start, numCoils);
					}
					else
					{
						// Oops! Setting the coils seems to have failed
						response.setError(request.getServerID(), request.getFunctionCode(), SERVER_DEVICE_FAILURE);
					}
				}
				else
				{
					// numBytes had a wrong value
					response.setError(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_VALUE);
				}
			}
			else
			{
				// The given set will not fit to our coil storage
				response.setError(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
			}
			return response;
		};
		_iot.registerMBWorkers(READ_INPUT_REGISTER, modbusFC04);
		_iot.registerMBWorkers(READ_COIL, modbusFC01);
		_iot.registerMBWorkers(READ_DISCR_INPUT, modbusFC02);
		_iot.registerMBWorkers(WRITE_COIL, modbusFC05);
		_iot.registerMBWorkers(WRITE_MULT_COILS, modbusFC0F);
	}

	void PLC::CleanUp()
	{
		_webSocket.cleanupClients(); // cleanup disconnected clients or too many clients
	}

	void PLC::Monitor()
	{
		for (int i = 0; i < _analogInputs; i++)
		{
			_AnalogSensors[i].Run();
		}
	}

	void PLC::Process()
	{
		_iot.Run();
		if (_iot.getNetworkState() == OnLine)
		{
			JsonDocument doc;
			doc.clear();
			for (int i = 0; i < _digitalInputs; i++)
			{
				doc[_DigitalSensors[i].Pin()] = _DigitalSensors[i].Level() ? "High" : "Low";
			}
			for (int i = 0; i < _analogInputs; i++)
			{
				doc[_AnalogSensors[i].Channel()] = _AnalogSensors[i].Level();
			}
			for (int i = 0; i < DO_PINS; i++)
			{
				doc[_Coils[i].Pin()] = _Coils[i].Level() ? "On" : "Off";
			}
			String s;
			serializeJson(doc, s);
			DeserializationError err = deserializeJson(doc, s);
			if (err)
			{
				loge("deserializeJson() failed: %s", err.c_str());
			}
			if (_lastMessagePublished == s) // anything changed?
			{
				return;
			}
			_iot.PublishOnline();
			_iot.Publish("readings", s.c_str(), false);
			_lastMessagePublished = s;
			_webSocket.textAll(s);
		}
	}

	void PLC::onMqttConnect()
	{
		if (ReadyToPublish())
		{
			logd("Publishing discovery ");
			char buffer[STR_LEN];
			JsonDocument doc;
			JsonObject device = doc["device"].to<JsonObject>();
			device["sw_version"] = CONFIG_VERSION;
			device["manufacturer"] = "ClassicDIY";
			sprintf(buffer, "ESP32-Bit (%X)", _iot.getUniqueId());
			device["model"] = buffer;

			JsonObject origin = doc["origin"].to<JsonObject>();
			origin["name"] = TAG;

			JsonArray identifiers = device["identifiers"].to<JsonArray>();
			sprintf(buffer, "%X", _iot.getUniqueId());
			identifiers.add(buffer);

			JsonObject components = doc["components"].to<JsonObject>();

			for (int i = 0; i < _digitalInputs; i++)
			{
				JsonObject din = components[_DigitalSensors[i].Pin()].to<JsonObject>();
				din["platform"] = "sensor";
				din["name"] = _DigitalSensors[i].Pin().c_str();
				sprintf(buffer, "%X_%s", _iot.getUniqueId(), _DigitalSensors[i].Pin().c_str());
				din["unique_id"] = buffer;
				sprintf(buffer, "{{ value_json.%s }}", _DigitalSensors[i].Pin().c_str());
				din["value_template"] = buffer;
				din["icon"] = "mdi:switch";
			}
			for (int i = 0; i < _analogInputs; i++)
			{
				JsonObject ain = components[_AnalogSensors[i].Channel()].to<JsonObject>();
				ain["platform"] = "sensor";
				ain["name"] = _AnalogSensors[i].Channel().c_str();
				ain["unit_of_measurement"] = "%";
				sprintf(buffer, "%X_%s", _iot.getUniqueId(), _AnalogSensors[i].Channel().c_str());
				ain["unique_id"] = buffer;
				sprintf(buffer, "{{ value_json.%s }}", _AnalogSensors[i].Channel().c_str());
				ain["value_template"] = buffer;
				ain["icon"] = "mdi:lightning-bolt";
			}
			for (int i = 0; i < DO_PINS; i++)
			{
				JsonObject dout = components[_Coils[i].Pin()].to<JsonObject>();
				dout["platform"] = "sensor";
				dout["name"] = _Coils[i].Pin().c_str();
				sprintf(buffer, "%X_%s", _iot.getUniqueId(), _Coils[i].Pin().c_str());
				dout["unique_id"] = buffer;
				sprintf(buffer, "{{ value_json.%s }}", _Coils[i].Pin().c_str());
				dout["value_template"] = buffer;
				dout["icon"] = "mdi:valve-open";
			}

			sprintf(buffer, "%s/stat/readings", _iot.getRootTopicPrefix().c_str());
			doc["state_topic"] = buffer;
			sprintf(buffer, "%s/tele/LWT", _iot.getRootTopicPrefix().c_str());
			doc["availability_topic"] = buffer;
			doc["pl_avail"] = "Online";
			doc["pl_not_avail"] = "Offline";

			_iot.PublishHADiscovery(doc);
			_discoveryPublished = true;
		}
	}

	void PLC::onMqttMessage(char *topic, JsonDocument &doc)
	{
		logd("onMqttMessage %s", topic);
		if (doc.containsKey("command"))
		{
			if (strcmp(doc["command"], "Write Coil") == 0)
			{
				int coil = doc["coil"];
				coil -= 1;
				if (coil >= 0 && coil < DO_PINS)
				{
					String input = doc["state"];
					input.toLowerCase();
					if (input == "on" || input == "high" || input == "1")
					{
						_Coils[coil].Set(HIGH);
						logi("Write Coil %d HIGH", coil);
					}
					else if (input == "off" || input == "low" || input == "0")
					{
						_Coils[coil].Set(LOW);
						logi("Write Coil %d LOW", coil);
					}
					else
					{
						logw("Write Coil %d invalid state", coil);
					}
				}
			}
		}
	}
}