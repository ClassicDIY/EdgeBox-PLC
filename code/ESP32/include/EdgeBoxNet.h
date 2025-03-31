#pragma once

#include <esp_err.h>
#include <esp_log.h>
#include <esp_eth.h>
#include "pins_arduino.h"

	class EdgeBoxNet
	{
	public:
		
		EdgeBoxNet() {};
		~EdgeBoxNet() {};
        esp_err_t Connect();

	private:
	};

