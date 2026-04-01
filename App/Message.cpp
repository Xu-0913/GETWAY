#include "Message.h"
using json = nlohmann::json;

/* 协议：1byte[type] 2byte[id_len] 3byte[data_len]  From4byte-->[id]+[data] */

Message::Message(ConnectionType type,const std::vector<uint8_t>& id,const std::vector<uint8_t>& data):connectionType_(type), id_(id), data_(data)
{
    if (id_.size() > 255 || data_.size() > 255)
    {
        throw std::runtime_error("Message length too large");
    }
}

std::vector<uint8_t> Message::toBinary() const
{
    if (id_.size() > 255 || data_.size() > 255)
    {
        throw std::runtime_error("Message length too large");
    }

    std::vector<uint8_t> binary;
    binary.reserve(3 + id_.size() + data_.size());

    binary.push_back(static_cast<uint8_t>(connectionType_));
    binary.push_back(static_cast<uint8_t>(id_.size()));
    binary.push_back(static_cast<uint8_t>(data_.size()));

    binary.insert(binary.end(), id_.begin(), id_.end());  // 开始放内容
    binary.insert(binary.end(), data_.begin(), data_.end());

    return binary;
}

std::string Message::toJson() const 
{
    json j;
    j["connection_type"] = static_cast<uint8_t>(connectionType_);
    j["id"] = bytesToHex(id_);
    j["data"] = bytesToHex(data_);

    return j.dump();  // 完成序列化 这样的序列化{"one":1,"two":2}
}

Message::ConnectionType Message::getConnectionType() const
{
    return connectionType_;
}

const std::vector<uint8_t>& Message::getId() const
{
    return id_;
}

const std::vector<uint8_t>& Message::getData() const
{
    return data_;
}

Message Message::fromBinary(const uint8_t* data, size_t len)
{
    if (data == nullptr)
    {
        throw std::runtime_error("Binary data is null");
    }

    if (len < 3)
    {
        throw std::runtime_error("Binary message too short");
    }

    uint8_t type = data[0];
    uint8_t idLen = data[1];
    uint8_t dataLen = data[2];

    if (len != static_cast<size_t>(3 + idLen + dataLen))
    {
        throw std::runtime_error("Binary message length invalid");
    }

    std::vector<uint8_t> id(data + 3, data + 3 + idLen);  // 放入(起始, 结束)内容到vector
    std::vector<uint8_t> payload(data + 3 + idLen, data + 3 + idLen + dataLen);

    return Message(static_cast<ConnectionType>(type), id, payload);
}

Message Message::fromJson(const std::string& jsonStr)
{
    json j = json::parse(jsonStr);

    if (!j.contains("connection_type") || !j.contains("id") || !j.contains("data"))
    {
        throw std::runtime_error("JSON missing required fields");
    }  // 检查 JSON 有没有缺字段

    if (!j["connection_type"].is_number_integer())
    {
        throw std::runtime_error("connection_type must be integer");
    }

    if (!j["id"].is_string() || !j["data"].is_string())
    {
        throw std::runtime_error("id/data must be string");  
    }

    int typeInt = j["connection_type"].get<int>();  // 检查必须在 0~255 之间
    if (typeInt < 0 || typeInt > 255)
    {
        throw std::runtime_error("connection_type out of range");
    }

    std::vector<uint8_t> id = hexToBytes(j["id"].get<std::string>());
    std::vector<uint8_t> data = hexToBytes(j["data"].get<std::string>());

    return Message(static_cast<ConnectionType>(typeInt), id, data);
}

std::string Message::bytesToHex(const std::vector<uint8_t>& data)
{
    std::ostringstream oss;
    oss << std::uppercase << std::hex << std::setfill('0');  // 输出两位大写十六进制不够补0

    for (uint8_t byte : data)
    {
        oss << std::setw(2) << static_cast<int>(byte);
    }

    return oss.str();
}

std::vector<uint8_t> Message::hexToBytes(const std::string& hexStr)
{
    if (hexStr.size() % 2 != 0)
    {
        throw std::runtime_error("Hex string length invalid");
    }

    auto hexCharToValue = [](char c)->uint8_t
    {
        if (c >= '0' && c <= '9'){return static_cast<uint8_t>(c - '0');}
        if (c >= 'a' && c <= 'f'){return static_cast<uint8_t>(c - 'a' + 10);}
        if (c >= 'A' && c <= 'F'){return static_cast<uint8_t>(c - 'A' + 10);}
        throw std::runtime_error("Hex string contains invalid character");
    };

    std::vector<uint8_t> bytes;
    bytes.reserve(hexStr.size() / 2);

    for (size_t i = 0; i < hexStr.size(); i += 2)
    {
        uint8_t high = hexCharToValue(hexStr[i]);
        uint8_t low  = hexCharToValue(hexStr[i + 1]);
        bytes.push_back(static_cast<uint8_t>((high << 4) | low));
    }

    return bytes;
}

int main()
{
        std::vector<uint8_t> id   = {0x01, 0x02, 0xAB, 0xCD};
        std::vector<uint8_t> data = {0x11, 0x22, 0x33, 0x44, 0x55};
        Message msg(Message::ConnectionType::LORA, id, data);
    return 0;
}




