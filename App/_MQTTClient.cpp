#include "_MQTTClient.h"
#include "_MQTTCallback.h"

MQTTClient::MQTTClient(const MQTTClientConfig& config) :client_(config.server, config.clientId),
                                                        config_(config),callback_(nullptr),connected_(false),
                                                        needResubscribe_(false),firstConnectDone_(false)
{
    callback_ = std::make_unique<MQTTCallback>(this);
    client_.set_callback(*callback_);

    connOpts_.set_keep_alive_interval(config_.keepAliveInterval);
    connOpts_.set_clean_session(config_.cleanSession);  // false才会再次订阅
    connOpts_.set_automatic_reconnect(true);
}

MQTTClient::~MQTTClient()
{
    close();
}

int MQTTClient::start()
{
    try
    {
        if (client_.is_connected())
        {
            connected_ = true;
            std::cout << "MQTTClient already started." << std::endl;
            return 0;
        }

        client_.connect(connOpts_)->wait();
        connected_ = true;

        std::cout << "MQTT connect success." << std::endl;

        {
            std::lock_guard<std::mutex> lock(subMutex_);
            client_.subscribe(config_.subTopic, config_.qos)->wait();
        }

        firstConnectDone_ = true;
        std::cout << "MQTT subscribe success. topic = " << config_.subTopic << std::endl;

        return 0;
    }
    catch (const mqtt::exception& e)
    {
        connected_ = false;
        std::cerr << "MQTTClient start failed: " << e.what() << std::endl;
        return -1;
    }
    catch (const std::exception& e)
    {
        connected_ = false;
        std::cerr << "MQTTClient start failed: " << e.what() << std::endl;
        return -1;
    }
    catch (...)
    {
        connected_ = false;
        std::cerr << "MQTTClient start failed: unknown exception" << std::endl;
        return -1;
    }
}

void MQTTClient::registerRecvCallback(std::function<int(const char*, int)> callback)
{
    Recvcb_ = std::move(callback);
}

int MQTTClient::send(const char* data, int len)
{
    try
    {
        if (!connected_ || !client_.is_connected())
        {
            std::cerr << "MQTTClient send skipped: client not connected." << std::endl;
            return -1;
        }

        if (data == nullptr || len <= 0)
        {
            std::cerr << "MQTTClient send failed: invalid payload." << std::endl;
            return -1;
        }

        std::lock_guard<std::mutex> lock(pubMutex_);

        auto token = client_.publish(config_.pubTopic, data, len, config_.qos, false);
        token->wait();

        std::cout << "MQTT publish success. topic = " << config_.pubTopic
                  << ", payload = " << std::string(data, len) << std::endl;
        return 0;
    }
    catch (const mqtt::exception& e)
    {
        connected_ = false;
        std::cerr << "MQTTClient publish failed: " << e.what() << std::endl;
        return -1;
    }
    catch (const std::exception& e)
    {
        connected_ = false;
        std::cerr << "MQTTClient publish failed: " << e.what() << std::endl;
        return -1;
    }
    catch (...)
    {
        connected_ = false;
        std::cerr << "MQTTClient publish failed: unknown exception" << std::endl;
        return -1;
    }
}

bool MQTTClient::tryResubscribe()
{
    if (!needResubscribe_) {return false;}

    if (!connected_ || !client_.is_connected()) {return false;}

    try
    {
        std::lock_guard<std::mutex> lock(subMutex_);
        client_.subscribe(config_.subTopic, config_.qos);
        needResubscribe_ = false;
        std::cout << "MQTT re-subscribe success. topic = " << config_.subTopic << std::endl;
        return true;
    }

    catch (const mqtt::exception& e)
    {
        std::cerr << "MQTT re-subscribe failed: " << e.what() << std::endl;
        return false;
    }

    catch (const std::exception& e)
    {
        std::cerr << "MQTT re-subscribe failed: " << e.what() << std::endl;
        return false;
    }

    catch (...)
    {
        std::cerr << "MQTT re-subscribe failed: unknown exception" << std::endl;
        return false;
    }
}

void MQTTClient::close()
{
    try
    {
        connected_ = false;
        needResubscribe_ = false;
    if (!client_.is_connected()){return;}

        client_.disconnect()->wait();
        std::cout << "MQTTClient disconnected." << std::endl;
    }

    catch (const mqtt::exception& e)
    {
        std::cerr << "MQTTClient close failed: " << e.what() << std::endl;
    }

    catch (const std::exception& e)
    {
        std::cerr << "MQTTClient close failed: " << e.what() << std::endl;
    }

    catch (...)
    {
        std::cerr << "MQTTClient close failed: unknown exception" << std::endl;
    }
}

void MQTTClient::onConnected(const std::string& cause)
{
    connected_ = true;
    std::cout << "MQTT connected";
    if (!cause.empty())
    {
        std::cout << ", cause = " << cause;
    }
    std::cout << std::endl;

    if (firstConnectDone_)
    {
        needResubscribe_ = true;
    }
}

void MQTTClient::onConnectionLost(const std::string& cause)
{
    connected_ = false;
    needResubscribe_ = true;

    std::cerr << "MQTT connection lost";
    if (!cause.empty()){std::cerr << ": " << cause;}
    std::cerr << std::endl;
}

void MQTTClient::onMessageArrived(mqtt::const_message_ptr msg)
{
    if (!msg)
    {
        std::cerr << "MQTT message arrived: null message." << std::endl;
        return;
    }

    const std::string payload = msg->to_string();

    std::cout << "Message arrived:"
              << "\n\ttopic: " << msg->get_topic()
              << "\n\tpayload: " << payload
              << std::endl;

    if (Recvcb_)
    {
        int ret = Recvcb_(payload.data(), static_cast<int>(payload.size()));
        if (ret != 0)
        {
            std::cerr << "Recv callback handle failed, ret = " << ret << std::endl;
        }
    }
    else
    {
        std::cerr << "Recv callback is not registered." << std::endl;
    }
}

void MQTTClient::onDeliveryComplete(mqtt::delivery_token_ptr token)
{
    if (token)
    {
        std::cout << "Message delivery complete, token id = "
                  << token->get_message_id() << std::endl;
    }
    else
    {
        std::cout << "Message delivery complete." << std::endl;
    }
}


int main()
{
    MQTTClient::MQTTClientConfig config;
    config.server = "tcp://172.21.8.0:1883";   
    config.clientId = "test_client_1";
    config.subTopic = "test/topic";
    config.pubTopic = "test/topic";
    config.qos = 1;
    config.keepAliveInterval = 20;
    config.cleanSession = false;

    MQTTClient client(config);

    client.registerRecvCallback([](const char* data, int len) -> int {
        std::cout << "[USER CALLBACK] recv: "
                  << std::string(data, len) << std::endl;
        return 0;
    });

    if (client.start() != 0)
    {
        std::cerr << "MQTT start failed." << std::endl;
        return -1;
    }

    client.send("hello mqtt", 10);

    std::cout << "Press Ctrl+C to exit..." << std::endl;

    while (true)
    {
        client.tryResubscribe();

        std::this_thread::sleep_for(std::chrono::seconds(5));
        client.send("ping", 4);
    }

    return 0;
}