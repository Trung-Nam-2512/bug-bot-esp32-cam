#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>

namespace net
{
    extern const char *__esp32cam_hmac_key; // <-- THÊM DÒNG NÀY (KHAI BÁO)

    class HttpUploader
    {
    public:
        bool postJpeg(const char *url,
                      const char *bearer,
                      const char *deviceId,
                      const uint8_t *data,
                      size_t len,
                      uint32_t tsSec,
                      const char *extraJson);

    private:
        bool sendMultipart(WiFiClient &client,
                           const String &host,
                           const String &path,
                           const String &bearer,
                           const String &signature, // <--- thêm tham số
                           const String &boundary,
                           const String &bodyStart,
                           const uint8_t *bin, size_t binLen,
                           const String &bodyEnd);
    };

} // namespace net
