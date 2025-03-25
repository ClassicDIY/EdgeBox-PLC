#include <Arduino.h>
#include "Log.h"
#include "Coil.h"

namespace ESP_PLC
{
	Coil::Coil(int sensorPin)
	{
		_sensorPin = sensorPin;
		pinMode(sensorPin, OUTPUT);
	}

	Coil::~Coil()
	{
	}

	std::string Coil::Pin()
	{
		std::stringstream ss;
		ss << "GPIO_" << _sensorPin;
		std::string formattedString = ss.str();
		return formattedString;
	}

	// level in 0 -> 100% range
	bool Coil::Level()
	{
		return (bool)digitalRead(_sensorPin);
	}

	void Coil::Set(uint8_t state)
	{
		digitalWrite(_sensorPin, state);
	}

} // namespace namespace ESP_PLC
