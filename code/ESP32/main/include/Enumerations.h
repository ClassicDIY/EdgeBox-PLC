#pragma once

namespace EDGEBOX
{
    enum NetworkSelection
    {
        NotConnected,
        APMode,
        WiFiMode,
        EthernetMode,
        ModemMode
    };

    enum NetworkState
    {
      Boot,
      ApMode,
      ConnectingWifi,
      ConnectingEthernet,
      ConnectingModem,
      OnLine,
      OffLine
    };
}