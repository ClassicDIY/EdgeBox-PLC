#pragma once
#include <Arduino.h>
#include <sstream> 
#include <string>
#include "defines.h"

namespace EDGEBOX
{
	class AnalogSensor
	{
	public:
		
		AnalogSensor(int channel);
		~AnalogSensor();
		std::string Channel();
		float Level();

	private:
		int _channel;
		float AddReading(float val);
		float _rollingSum;
		int _numberOfSummations;
		int _count;
	};
}
