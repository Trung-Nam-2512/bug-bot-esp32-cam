#pragma once
#include <WiFi.h>

namespace net
{

    class WiFiHelper
    {
    public:
        void begin(const char *const *ssids, const char *const *passes, int count);
        bool ensure();
        bool connected() const;

    private:
        const char *const *ssids_ = nullptr;
        const char *const *passes_ = nullptr;
        int count_ = 0;
        uint32_t lastTry_ = 0;
    };

} // namespace net
