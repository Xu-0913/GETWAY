#include "_MQTTCallback.h"
#include "_MQTTClient.h"

void MQTTCallback::connection_lost(const std::string& cause)
{
    if (owner_ != nullptr)
    {
        owner_->onConnectionLost(cause);
    }
}

void MQTTCallback::message_arrived(mqtt::const_message_ptr msg)
{
    if (owner_ != nullptr)
    {
        owner_->onMessageArrived(msg);
    }
}

void MQTTCallback::delivery_complete(mqtt::delivery_token_ptr token)
{
    if (owner_ != nullptr)
    {
        owner_->onDeliveryComplete(token);
    }
}

void MQTTCallback::connected(const std::string& cause)
{
    if (owner_ != nullptr)
    {
        owner_->onConnected(cause);
    }
}