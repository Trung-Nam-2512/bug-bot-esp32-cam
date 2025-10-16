#include "MqttClient.h"

using namespace net;

static MqttClient *_gSelf = nullptr;

void MqttClient::begin(const char *host, uint16_t port, const char *user, const char *pass,
                       const char *clientId, const char *topicSubscribe, MessageHandler onMsg)
{
    host_ = host;
    port_ = port;
    user_ = user ? user : "";
    pass_ = pass ? pass : "";
    clientId_ = clientId ? clientId : "cam-01";
    subTopic_ = topicSubscribe ? topicSubscribe : "";
    handler_ = onMsg;

    mqtt_.setServer(host_.c_str(), port_);
    _gSelf = this;
    mqtt_.setCallback([](char *t, byte *p, unsigned int l)
                      { if (_gSelf) _gSelf->_staticCb(t,p,l); });
}

void MqttClient::_staticCb(char *topic, byte *payload, unsigned int length)
{
    if (!_gSelf || !_gSelf->handler_)
        return;
    _gSelf->handler_(String(topic), payload, length);
}

bool MqttClient::ensure()
{
    if (mqtt_.connected())
        return true;
    if (millis() - lastRetry_ < 2000)
        return false;
    lastRetry_ = millis();

    if (user_.length() > 0)
    {
        if (!mqtt_.connect(clientId_.c_str(), user_.c_str(), pass_.c_str()))
            return false;
    }
    else
    {
        if (!mqtt_.connect(clientId_.c_str()))
            return false;
    }

    if (subTopic_.length() > 0)
        mqtt_.subscribe(subTopic_.c_str());
    return true;
}

void MqttClient::loop()
{
    if (!mqtt_.loop())
    {
        // không làm gì, ensure() sẽ reconnect theo nhịp riêng
    }
}

bool MqttClient::publish(const char *topic, const char *payload, bool retain)
{
    if (!mqtt_.connected())
        return false;
    return mqtt_.publish(topic, payload, retain);
}
