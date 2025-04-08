#pragma once
#include <Arduino.h>
#include <sstream> 
#include <string>
#include "defines.h"

namespace EDGEBOX
{
	class DigitalSensor
	{
	public:
		
		DigitalSensor(int sensorPin);
		~DigitalSensor();
		std::string Pin();
		bool Level();

	private:
		int _sensorPin; // Defines the pin that the sensor is connected to
	};
}
