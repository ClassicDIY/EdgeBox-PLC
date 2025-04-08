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

	float AnalogSensor::Level()
	{
		int16_t adcReading = ads.readADC_SingleEnded(_channel);
		float percent = (adcReading * 100) / ADC_Resolution;
		float averagePercent = AddReading(percent);
		averagePercent = roundf(averagePercent* 100.0)/ 100.0; // round to 0 decimal place
		#ifdef LOG_SENSOR_VOLTAGE
		if (_count++ > 100)
		{
			logd("Sensor Reading: %d percent: %f, averagePercent:%f", adcReading, percent , averagePercent);
			_count = 0;
		}
		#endif
		return averagePercent;
	}

	float AnalogSensor::AddReading(float val)
	{
		float currentAvg = 0.0;
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
		return _rollingSum / _numberOfSummations;
	}
} // namespace namespace EDGEBOX
