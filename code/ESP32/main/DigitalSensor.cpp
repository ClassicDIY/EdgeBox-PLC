#include <Arduino.h>
#include "Log.h"
#include "DigitalSensor.h"

namespace EDGEBOX
{
	DigitalSensor::DigitalSensor(int sensorPin)
	{
		_sensorPin = sensorPin;
		pinMode(sensorPin, INPUT_PULLUP);
	}

	DigitalSensor::~DigitalSensor()
	{
	}

	std::string DigitalSensor::Pin()
	{
		std::stringstream ss;
		ss << "GPIO_" << _sensorPin;
		std::string formattedString = ss.str();
		return formattedString;
	}

	// level in 0 -> 100% range
	bool DigitalSensor::Level()
	{
		return (bool)digitalRead(_sensorPin);
	}

} // namespace namespace EDGEBOX
