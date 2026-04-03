#include "run.h"
#include "Route.h"
#include "bluetooth.h"
#include "Serial.h"
#include "ThreadPool.h"
#include "_MQTTClient.h"

#include <atomic>
#include <csignal>
#include <chrono>
#include <thread>
#include <iostream>

namespace
{
    std::atomic<bool> g_running{false};

    void signalHandler(int sig)
    {
        (void)sig;
        g_running = false;
    }

    void installSignals()
    {
        std::signal(SIGINT, signalHandler);
        std::signal(SIGTERM, signalHandler);
    }
}

void stopGateway()
{
    g_running = false;
}

int runGateway()
{
    installSignals();
    g_running = true;

    try
    {
        // 1. 创建线程池
        ThreadPool threadPool(5, "gateway");

        // 2. 配置 MQTT
        MQTTClient::MQTTClientConfig mqttConfig;
        mqttConfig.server = "tcp://172.21.8.0:1883";   // 改成你的 broker 地址
        mqttConfig.clientId = "gateway_client_1";
        mqttConfig.subTopic = "test/topic";
        mqttConfig.pubTopic = "test/topic";
        mqttConfig.qos = 1;
        mqttConfig.keepAliveInterval = 20;
        mqttConfig.cleanSession = false;

        MQTTClient mqttClient(mqttConfig);

        // 3. 创建路由
        Route route(mqttClient);
        if (route.init() != 0)
        {
            std::cerr << "runGateway failed: route init failed." << std::endl;
            return -1;
        }

        // 4. 创建设备
        BluetoothDevice bluetooth("/dev/ttyS0", threadPool);   // 改成你的实际设备名

        // 5. 串口配置
        Serial serial(bluetooth);
        Serial::Config serialCfg;
        serialCfg.baudRate = Serial::BaudRate::BR115200;
        serialCfg.stopBits = Serial::StopBits::One;
        serialCfg.parity = Serial::Parity::None;
        serialCfg.blocking = false;

        if (!serial.reconfigure(serialCfg))
        {
            std::cerr << "runGateway failed: serial config failed." << std::endl;
            return -1;
        }

        // 6. 注册设备到路由
        if (route.registerDevice(bluetooth) != 0)
        {
            std::cerr << "runGateway failed: register device failed." << std::endl;
            return -1;
        }

        std::cout << "Gateway is running..." << std::endl;

        // 7. 主循环
        while (g_running)
        {
            mqttClient.tryResubscribe();
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        std::cout << "Gateway is stopping..." << std::endl;
        route.close();
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "runGateway exception: " << e.what() << std::endl;
        return -1;
    }
    catch (...)
    {
        std::cerr << "runGateway exception: unknown exception" << std::endl;
        return -1;
    }
}