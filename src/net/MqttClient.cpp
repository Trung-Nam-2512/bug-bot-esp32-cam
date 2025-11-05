#include "MqttClient.h"
using namespace net;

static MqttClient *_gSelf = nullptr;

void MqttClient::begin(const char *host, uint16_t port, const char *user, const char *pass,
                       const char *clientId, const char *topicSubscribe, MessageHandler onMsg)
{
    host_ = host ? host : "";
    port_ = port;
    user_ = user ? user : "";
    pass_ = pass ? pass : "";
    clientId_ = clientId ? clientId : "esp32cam";
    subTopic_ = topicSubscribe ? topicSubscribe : "";
    handler_ = onMsg;

    mqtt_.setServer(host_.c_str(), port_);
    mqtt_.setKeepAlive(keepAlive_);
    mqtt_.setSocketTimeout(socketTimeout_);
    mqtt_.setBufferSize(bufferSize_);
    mqtt_.setCallback([](char *t, byte *p, unsigned int l)
                      {
    if (_gSelf) _gSelf->_staticCb(t, p, l); });
    _gSelf = this;
}

void MqttClient::setKeepAlive(uint16_t seconds)
{
    keepAlive_ = seconds;
    mqtt_.setKeepAlive(keepAlive_);
}
void MqttClient::setSocketTimeout(uint16_t seconds)
{
    socketTimeout_ = seconds;
    mqtt_.setSocketTimeout(socketTimeout_);
}
void MqttClient::setBufferSize(uint16_t bytes)
{
    bufferSize_ = bytes;
    mqtt_.setBufferSize(bufferSize_);
}
void MqttClient::setCleanSession(bool clean)
{
    cleanSession_ = clean;
}
void MqttClient::setWill(const char *topic, const char *payload, bool retain, int qos)
{
    hasWill_ = (topic && *topic);
    willTopic_ = hasWill_ ? topic : "";
    willPayload_ = payload ? payload : "";
    willRetain_ = retain;
    willQos_ = (qos == 0 || qos == 1) ? qos : 1;
}

void MqttClient::_staticCb(char *topic, byte *payload, unsigned int length)
{
    if (!handler_)
        return;
    handler_(String(topic), payload, length);
}

bool MqttClient::ensure()
{
    if (mqtt_.connected())
    {
        return true;
    }

    if (millis() - lastRetry_ < 2000)
        return false;
    lastRetry_ = millis();

    bool ok = false;
    if (user_.length() > 0)
    {
        if (hasWill_)
        {
            ok = mqtt_.connect(clientId_.c_str(),
                               user_.c_str(), pass_.c_str(),
                               willTopic_.c_str(), willQos_, willRetain_, willPayload_.c_str());
        }
        else
        {
            ok = mqtt_.connect(clientId_.c_str(),
                               user_.c_str(), pass_.c_str());
        }
    }
    else
    {
        if (hasWill_)
        {
            ok = mqtt_.connect(clientId_.c_str(),
                               willTopic_.c_str(), willQos_, willRetain_, willPayload_.c_str());
        }
        else
        {
            ok = mqtt_.connect(clientId_.c_str());
        }
    }

    if (!ok)
    {
        subscribed_ = false;
        return false;
    }

    subscribed_ = false;
    if (subTopic_.length() > 0)
    {
        subscribed_ = mqtt_.subscribe(subTopic_.c_str(), subQos_);
    }

    return true;
}

void MqttClient::loop()
{
    // PubSubClient yêu cầu loop() chạy thường xuyên để xử lý inbound/outbound
    mqtt_.loop();
}

bool MqttClient::publish(const char *topic, const char *payload, bool retain, int qos)
{
    if (!mqtt_.connected() || !topic)
        return false;
    // PubSubClient hỗ trợ QoS 0/1
    int q = (qos == 0 || qos == 1) ? qos : 0;
    const char *msg = payload ? payload : "";
    return mqtt_.publish(topic, (const uint8_t *)msg, strlen(msg), retain);
}

bool MqttClient::subscribe(const char *topic, int qos)
{
    if (!mqtt_.connected() || !topic)
        return false;
    int q = (qos == 0 || qos == 1) ? qos : 1;
    return mqtt_.subscribe(topic, q);
}
