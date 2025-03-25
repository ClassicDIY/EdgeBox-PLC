#pragma once
#include <Arduino.h>
#include <sstream> 
#include <string>
#include "defines.h"

namespace ESP_PLC
{
	class Coil
	{
	public:
		
		Coil(int sensorPin);
		~Coil();
		std::string Pin();
		bool Level();
		void Set(uint8_t state);

	private:
		int _sensorPin; // Defines the pin that the sensor is connected to
	};
}
