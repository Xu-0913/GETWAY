#pragma once
#include "/home/xu/GetWay/Public/_public.h"
#include "Device.h"
#include "_MQTTClient.h"
#include "Message.h"
constexpr std::size_t kMaxDeviceNum = 10; // 宏定义

class Route
{
    private:
        MQTTClient& mqttClient_;
        std::vector<Device*> devices_;
        bool initialized_{false};

    public:
        Route(MQTTClient& mqttClient);
        ~Route();

        int init();
        int registerDevice(Device& device);
        void close();

    private:
        int handleMqttMessage(const char* jsonStr, int len);
        int handleDeviceMessage(void* data, int len);
        Device* findDeviceByType(Message::ConnectionType type) const;
};