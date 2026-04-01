#pragma once
#include "/home/xu/GetWay/Public/_public.h"
#include <atomic>
#include <mutex>

class MQTTCallback;

class MQTTClient
{
public:
    struct MQTTClientConfig
    {
        std::string server;            // tcp://127.0.0.1:1883
        std::string clientId;          // 客户端ID
        std::string subTopic;          // 订阅主题
        std::string pubTopic;          // 发布主题
        int qos;                       // QoS
        int keepAliveInterval;         // 心跳时间
        bool cleanSession;             // 是否清理会话
    };

private:
    mqtt::async_client client_;
    mqtt::connect_options connOpts_;
    MQTTClientConfig config_;
    std::unique_ptr<MQTTCallback> callback_;
    std::function<int(const char*, int)> Recvcb_;

    std::atomic<bool> connected_;
    std::atomic<bool> needResubscribe_;
    std::atomic<bool> firstConnectDone_;

    std::mutex pubMutex_;
    std::mutex subMutex_;

public:
    explicit MQTTClient(const MQTTClientConfig& config);
    ~MQTTClient();

    int start();
    int send(const char* data, int len);
    int tryResubscribe();
    void close();

    void registerRecvCallback(std::function<int(const char*, int)> callback);

public:
    void onConnected(const std::string& cause);
    void onConnectionLost(const std::string& cause);
    void onMessageArrived(mqtt::const_message_ptr msg);
    void onDeliveryComplete(mqtt::delivery_token_ptr token);
};