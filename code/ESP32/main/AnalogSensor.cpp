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

	float AnalogSensor::Level(int16_t min, int16_t max)
	{
		uint16_t delta = abs(max - min);
		float inc = delta / 16.0; // (20 - 4 ma) / delta => increment from min to max
		int16_t adcReading = ads.readADC_SingleEnded(_channel);
		float val = AddReading((adcReading * 100) / ADC_Resolution);
		val -= 4.0;
		if (val < 0) return 0.0; // minimum value is 4ma, not connected to 4-20 device!
		val *= inc;
		val = roundf(val*10.0)/10.0; // round to 1 decimal place
		#ifdef LOG_SENSOR_VOLTAGE
		if (_count++ > 100)
		{
			logd("Sensor Reading: %d inc: %d, val:%f", adcReading, inc , val);
			_count = 0;
		}
		#endif
		return val;
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
