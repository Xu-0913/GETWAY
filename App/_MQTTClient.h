#pragma once
#include "/home/xu/GetWay/Public/_public.h"

class MQTTCallback;
class MQTTClient
{
    friend class MQTTCallback; 
    public:
        struct MQTTClientConfig
        {
            std::string server;
            std::string clientId;
            std::string subTopic;
            std::string pubTopic;
            int qos;
            int keepAliveInterval;
            bool cleanSession;
        };

    private:
        
        mqtt::async_client client_;  // 异步客户端
        mqtt::connect_options connOpts_;
        std::unique_ptr<MQTTCallback> callback_;  // 回调类指针

        MQTTClientConfig config_;                     
        std::function<int(const char*, int)> Recvcb_;

        // 二传手回调函数
        void onConnectionLost(const std::string& cause);
        void onMessageArrived(mqtt::const_message_ptr msg);
        void onDeliveryComplete(mqtt::delivery_token_ptr token);
        void onConnected(const std::string& cause);

    public:
        MQTTClient(const MQTTClientConfig& config);
        ~MQTTClient();
        MQTTClient(const MQTTClient&) = delete;
        MQTTClient& operator=(const MQTTClient&) = delete;
        
        // 接口
        void registerRecvCallback(std::function<int(const char*, int)> callback);
        int send(const char* data, int len);
        int start();
        void close();
};




