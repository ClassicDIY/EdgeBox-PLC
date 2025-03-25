#pragma once

namespace ESP_PLC
{
    enum NetworkStatus
    {
        NotConnected,
        APMode,
        WiFiMode,
        EthernetMode

    } ;

    enum NetworkState
    {
      Boot,
      ApMode,
      Connecting,
      OnLine,
      OffLine
    };
}