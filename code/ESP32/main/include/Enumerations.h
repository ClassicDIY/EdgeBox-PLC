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
      ApState,
      Connecting,
      OnLine,
      OffLine
    };
}