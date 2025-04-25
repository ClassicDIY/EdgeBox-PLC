#include <Arduino.h>
#include <Adafruit_ADS1X15.h>
#include "Log.h"
#include "Defines.h"
#include "AnalogSensor.h"

extern Adafruit_ADS1115 ads;

namespace EDGEBOX
{
	AnalogSensor::AnalogSensor(int channel)
	{
		_channel = channel;
		_count = 0;
		_numberOfSummations = 0;
		_rollingSum = 0;
	}

	AnalogSensor::~AnalogSensor()
	{
	}

	std::string AnalogSensor::Channel()
	{
		std::stringstream ss;
		ss << "AI" << _channel;
		std::string formattedString = ss.str();
		return formattedString;
	}

	void AnalogSensor::Run()
	{
		AddReading(ads.readADC_SingleEnded(_channel));
	}

	void AnalogSensor::AddReading(uint32_t val)
	{
		if (val < adcReadingMin) // discard out of range readings
		{
			val = adcReadingMin; 
		}
		else if (val > adcReadingMax)
		{
			val = adcReadingMax;
		}
		int32_t currentAvg = 0;
		if (_numberOfSummations > 0)
		{
			currentAvg = _rollingSum / _numberOfSummations;
		}
		if (_numberOfSummations < SAMPLESIZE)
		{
			_numberOfSummations++;
		}
		else
		{
			_rollingSum -= currentAvg;
		}
		_rollingSum += val;
		return;
	}

	float AnalogSensor::Level()
	{
		return roundf(((((_rollingSum / _numberOfSummations) - adcReadingMin) * (_maxT - _minT)) / (adcReadingMax - adcReadingMin) + _minT) * 10.0) / 10.0;
	}
} // namespace namespace EDGEBOX
