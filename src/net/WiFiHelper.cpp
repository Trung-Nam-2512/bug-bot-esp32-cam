#include "WiFiHelper.h"
#include <Arduino.h>

using namespace net;

void WiFiHelper::begin(const char *const *ssids, const char *const *passes, int count)
{
    ssids_ = ssids;
    passes_ = passes;
    count_ = count;
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);

    for (int i = 0; i < count_; ++i)
    {
        WiFi.begin(ssids_[i], passes_[i]);
        for (int t = 0; t < 40; ++t)
        { // ~20s
            if (WiFi.status() == WL_CONNECTED)
            {
                Serial.printf("[I] WiFi OK: %s, IP=%s\n", ssids_[i], WiFi.localIP().toString().c_str());
                return;
            }
            delay(500);
        }
        WiFi.disconnect(true, true);
        delay(200);
    }
    Serial.println("[W] WiFi init failed, will retry in loop");
}

bool WiFiHelper::connected() const
{
    return WiFi.status() == WL_CONNECTED;
}

bool WiFiHelper::ensure()
{
    if (connected())
        return true;
    if (millis() - lastTry_ < 5000)
        return false;
    lastTry_ = millis();

    WiFi.disconnect(true, true);
    delay(100);
    for (int i = 0; i < count_; ++i)
    {
        WiFi.begin(ssids_[i], passes_[i]);
        for (int t = 0; t < 20; ++t)
        { // ~10s
            if (connected())
            {
                Serial.printf("[I] Reconnected: %s, IP=%s\n", ssids_[i], WiFi.localIP().toString().c_str());
                return true;
            }
            delay(500);
        }
    }
    Serial.println("[W] Reconnect failed");
    return false;
}
