#include "Route.h"

Route::Route(MQTTClient& mqttClient): mqttClient_(mqttClient)
{

}

Route::~Route()
{
    close();
}

int Route::init()
{
    if (initialized_) {return 0;}
    if (mqttClient_.start() != 0)
    {
        std::cerr << "Route init failed: mqtt start failed." << std::endl;
        return -1;
    }

    mqttClient_.registerRecvCallback([this](const char* jsonStr, int len) -> int{return this->handleMqttMessage(jsonStr, len);});
    initialized_ = true;
    return 0;
}

int Route::registerDevice(Device& device)
{
    if (!initialized_) {std::cerr << "Route registerDevice failed: Route not initialized." << std::endl;return -1;}
    if (devices_.size() >= kMaxDeviceNum) {std::cerr << "Route registerDevice failed: too many devices." << std::endl;return -1;}

    devices_.push_back(&device);

    device.registerRecvCallback([this](void* data, int len) -> int{return this->handleDeviceMessage(data, len);});
    if (!device.start())
    {
        std::cerr << "Route registerDevice failed: device start failed." << std::endl;
        devices_.pop_back();
        return -1;
    }

    return 0;
}

void Route::close()
{
    for (Device* device : devices_)
    {
        if (device != nullptr) {device->stop();device->close();}
    }

    devices_.clear();
    mqttClient_.close();
    initialized_ = false;
}

int Route::handleMqttMessage(const char* jsonStr, int len)
{
    if (jsonStr == nullptr || len <= 0) {std::cerr << "Route handleMqttMessage failed: invalid input." << std::endl;return -1;}

    try
    {
        std::string jsonStrCopy(jsonStr, static_cast<std::size_t>(len));
        Message message = Message::fromJson(jsonStrCopy);
        std::vector<uint8_t> binary = message.toBinary();

        Device* targetDevice = findDeviceByType(message.getConnectionType());
        if (targetDevice == nullptr)
        {
            std::cerr << "Route handleMqttMessage failed: no matching device." << std::endl;
            return -1;
        }

        if (!targetDevice->write(binary.data(), static_cast<int>(binary.size())))
        {
            std::cerr << "Route handleMqttMessage failed: device write failed." << std::endl;
            return -1;
        }

        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Route handleMqttMessage exception: " << e.what() << std::endl;
        return -1;
    }
}

int Route::handleDeviceMessage(void* data, int len)
{
    if (data == nullptr || len <= 0)
    {
        std::cerr << "Route handleDeviceMessage failed: invalid input." << std::endl;
        return -1;
    }

    try
    {
        const uint8_t* binary = reinterpret_cast<const uint8_t*>(data);
        Message message = Message::fromBinary(binary, static_cast<std::size_t>(len));
        std::string jsonStr = message.toJson();

        if (mqttClient_.send(jsonStr.c_str(), static_cast<int>(jsonStr.size())) != 0)
        {
            std::cerr << "Route handleDeviceMessage failed: mqtt send failed." << std::endl;
            return -1;
        }

        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Route handleDeviceMessage exception: " << e.what() << std::endl;
        return -1;
    }
}

Device* Route::findDeviceByType(Message::ConnectionType type) const
{
    for (auto x : devices_)
    {
        if (x != nullptr && x->connectionType() == type)
        {
            return x;
        }
    }
    return nullptr;
}