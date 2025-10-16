#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

namespace net
{

    class MqttClient
    {
    public:
        using MessageHandler = std::function<void(const String &topic, const uint8_t *payload, unsigned int len)>;

        void begin(const char *host, uint16_t port, const char *user, const char *pass,
                   const char *clientId, const char *topicSubscribe, MessageHandler onMsg);

        void loop();   // gọi đều trong loop()
        bool ensure(); // đảm bảo kết nối (reconnect nếu rớt)
        bool publish(const char *topic, const char *payload, bool retain = false);

    private:
        WiFiClient wifi_;
        PubSubClient mqtt_{wifi_};
        String host_; // giữ lại để reconnect
        uint16_t port_{1886};
        String user_;
        String pass_;
        String clientId_;
        String subTopic_;
        MessageHandler handler_;
        uint32_t lastRetry_{0};

        static void _staticCb(char *topic, byte *payload, unsigned int length);
    };

} // namespace net
