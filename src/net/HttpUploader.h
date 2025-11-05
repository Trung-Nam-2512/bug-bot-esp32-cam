#pragma once
#include <Arduino.h>
#include <Client.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

namespace net
{

    // Định nghĩa ở main.cpp:  namespace net { const char* __esp32cam_hmac_key = cfg::HMAC_KEY; }
    extern const char *__esp32cam_hmac_key;

    class HttpUploader
    {
    public:
        // url: http://... hoặc https://...
        // bearer: "Bearer xxx" hoặc nullptr/""
        // extraJson: JSON text (có thể nullptr/"")
        // shotId: id duy nhất mỗi lần chụp (để server de-dup). Có thể nullptr/""
        bool postJpeg(const char *url,
                      const char *bearer,
                      const char *deviceId,
                      const uint8_t *data,
                      size_t len,
                      uint32_t tsSec,
                      const char *extraJson,
                      const char *shotId);

    private:
        struct UrlParts
        {
            bool https = false;
            String host;
            uint16_t port = 80;
            String path = "/";
        };

        void splitUrl(const char *url, UrlParts &out);
        int readHttpStatus(Client &client, uint32_t waitMs = 10000);

        bool sendMultipart(Client &client,
                           const UrlParts &u,
                           const String &bearer,
                           const String &signature,
                           const String &shotId,
                           const String &boundary,
                           const String &bodyStart,
                           const uint8_t *bin, size_t binLen,
                           const String &bodyEnd);
    };

} // namespace net
