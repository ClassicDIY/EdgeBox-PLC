#pragma once

#include <esp_netif.h>
#include "pins_arduino.h"
#include "defines.h"
#include "IOT.h"

namespace EDGEBOX
{
	class EdgeBoxNet
	{
	public:
		esp_err_t ConnectEthernet(IOT* piot);
		esp_err_t ConnectModem(IOT* piot);

	private:
		void wakeup_modem(void);
		void start_network(void);
		esp_netif_t *_netif;
	};
}
