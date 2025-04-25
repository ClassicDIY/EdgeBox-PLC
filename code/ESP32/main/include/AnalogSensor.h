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
		void Run();
		float minV() { return _minV; }
		float minT() { return _minT; }
		float maxV() { return _maxV; }
		float maxT() { return _maxT; }		

		// .00038 V per ADC count for 4-20mA => 2635 counts = 1V => 4mA, 13175 counts = 5V => 20mA
		// max adc range 0-26350 for 0V -> 10V
		void SetMinV(float minV) { _minV = minV; adcReadingMin = minV * 2635; }
		void SetMinT(float minT) { _minT = minT; }
		void SetMaxV(float maxV) { _maxV = maxV; adcReadingMax = maxV * 2635;}
		void SetMaxT(float maxT) { _maxT = maxT; }
		void SetChannel(int channel) { _channel = channel; }

	private:
		int _channel;
		void AddReading(uint32_t val);
		uint32_t _rollingSum;
		int _numberOfSummations;
		int _count;
		float _minV = 1.0; // default to 4-20mA
		float _minT = 0;
		float _maxV = 5.0;
		float _maxT = 100.0;
		uint32_t adcReadingMin = 2635;
		uint32_t adcReadingMax = 13175;
	};
}
