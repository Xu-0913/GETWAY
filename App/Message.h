#pragma once
#include "/home/xu/GetWay/Public/_public.h"

class Message
{
    public:
        enum class ConnectionType : uint8_t
        {
            NONE = 0,
            LORA = 1,
            BLE_MESH = 2,
        };
    private: 
        ConnectionType connectionType_{ConnectionType::NONE};
        std::vector<uint8_t> id_;
        std::vector<uint8_t> data_;

    public:

        Message() = default;
        Message(ConnectionType type,const std::vector<uint8_t>& id, const std::vector<uint8_t>& data);
        ~Message() = default;

        std::vector<uint8_t> toBinary() const;// 把消息打包，变成能发给别人的字节流
        std::string toJson() const;    // 收到别人发来的字节流，解析成程序能用的消息
        ConnectionType getConnectionType() const;
        const std::vector<uint8_t>& getId() const;
        const std::vector<uint8_t>& getData() const;

        // 静态方法
        static Message fromBinary(const uint8_t* data, size_t len);  // 二进制解析
        static Message fromJson(const std::string& jsonStr);  // JSON 字符串解析
        static std::string bytesToHex(const std::vector<uint8_t>& data);    // bytes -> hex string
        static std::vector<uint8_t> hexToBytes(const std::string& hexStr);  // hex string -> bytes
};