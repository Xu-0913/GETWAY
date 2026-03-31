#pragma once
#include "/home/xu/GetWay/Public/_public.h"


class MQTTClient;   

class MQTTCallback: public virtual mqtt::callback
{
private:
    MQTTClient* owner_;   // 指向宿主 MQTTClient

public:
    MQTTCallback(MQTTClient* owner): owner_(owner){}
    ~MQTTCallback() = default;

    void connection_lost(const std::string& cause) override;    // 连接断开
    void message_arrived(mqtt::const_message_ptr msg) override;    // 收到消息
    void delivery_complete(mqtt::delivery_token_ptr token) override;    // 发送完成
    void connected(const std::string& cause) override;
};