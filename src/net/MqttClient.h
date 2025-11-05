#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

namespace net
{

    class MqttClient
    {
    public:
        using MessageHandler = std::function<void(const String &, const uint8_t *, unsigned int)>;

        // topicSubscribe: topic lệnh (ví dụ BINHDUONG/ESP32CAM/cam-01/cmd)
        void begin(const char *host, uint16_t port,
                   const char *user, const char *pass,
                   const char *clientId,
                   const char *topicSubscribe,
                   MessageHandler onMsg);

        // Tùy chọn: cấu hình KeepAlive/Timeout/Buffer/Will (gọi sau begin(), trước ensure())
        void setKeepAlive(uint16_t seconds);     // mặc định 30s
        void setSocketTimeout(uint16_t seconds); // mặc định 10s
        void setBufferSize(uint16_t bytes);      // mặc định 2048
        void setCleanSession(bool clean);        // mặc định true
        void setWill(const char *topic, const char *payload, bool retain = true, int qos = 1);

        bool ensure(); // kết nối + subscribe QoS1 (nếu chưa)
        void loop();   // gọi rất thường xuyên trong loop()
        bool publish(const char *topic, const char *payload,
                     bool retain = false, int qos = 0);

        bool subscribe(const char *topic, int qos = 1); // thêm sub phụ nếu cần
        bool connected() { return mqtt_.connected(); }
        bool isReady() { return mqtt_.connected() && subscribed_; }

    private:
        void _staticCb(char *topic, byte *payload, unsigned int length);

        WiFiClient net_;
        PubSubClient mqtt_{net_};

        String host_;
        uint16_t port_ = 1883;
        String user_, pass_, clientId_;
        String subTopic_;
        int subQos_ = 1;

        // Will
        bool hasWill_ = false;
        String willTopic_, willPayload_;
        bool willRetain_ = true;
        int willQos_ = 1;

        // options
        uint16_t keepAlive_ = 30;
        uint16_t socketTimeout_ = 10;
        uint16_t bufferSize_ = 2048;
        bool cleanSession_ = true;

        MessageHandler handler_;
        uint32_t lastRetry_ = 0;
        bool subscribed_ = false;
    };

} // namespace net
