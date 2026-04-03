#include "bluetooth.h"
#include <cstring>
#include <iostream>
#include <algorithm>

BluetoothDevice::BluetoothDevice(const std::string& filename,
                                 ThreadPool& threadPool,
                                 std::size_t bufferSize)
    : Device(filename, Message::ConnectionType::BLE_MESH, threadPool, bufferSize)
{
}

void BluetoothDevice::ignoreBuffer(int n)
{
    if (n <= 0) return;
    if (n >= readBufferLen_)
    {
        readBufferLen_ = 0;
        return;
    }

    std::memmove(readBuffer_, readBuffer_ + n, static_cast<std::size_t>(readBufferLen_ - n));
    readBufferLen_ -= n;
}

int BluetoothDevice::postRead(void* ptr, int& len)
{
    if (ptr == nullptr || len <= 0)
    {
        len = 0;
        return -1;
    }

    unsigned char* data = static_cast<unsigned char*>(ptr);

    // 1. 先把本次 read 到的数据拼到内部缓存
    int writable = static_cast<int>(READ_BUFFER_CAPACITY) - readBufferLen_;
    if (len > writable)
    {
        std::cerr << "BluetoothDevice postRead failed: internal read buffer full." << std::endl;
        len = 0;
        return -1;
    }

    std::memcpy(readBuffer_ + readBufferLen_, data, static_cast<std::size_t>(len));
    readBufferLen_ += len;

    // 2. 长度不足，先不往上层交
    if (readBufferLen_ < 4)
    {
        len = 0;
        return 0;
    }

    // 3. 扫描 ACK 或蓝牙数据帧
    for (int i = 0; i <= readBufferLen_ - 4; ++i)
    {
        // ACK: "OK\r\n"
        if (std::memcmp(readBuffer_ + i, "OK\r\n", 4) == 0)
        {
            ignoreBuffer(i + 4);
            len = 0;
            return 0;
        }

        // 蓝牙帧头: 0xF1 0xDD
        if (readBuffer_[i] == FIX_HEADER_0 && readBuffer_[i + 1] == FIX_HEADER_1)
        {
            // 丢掉前面无效数据
            ignoreBuffer(i);

            // 现在帧头一定在 readBuffer_[0]
            if (readBufferLen_ < 3)
            {
                len = 0;
                return 0;
            }

            int frameLen = static_cast<int>(readBuffer_[2]) + 3;  
            // C版判断就是 read_buffer[2] + 3

            if (readBufferLen_ < frameLen)
            {
                len = 0;
                return 0;   // 半包
            }

            // 蓝牙原始帧格式按你旧代码逻辑：
            // [0]=0xF1 [1]=0xDD [2]=长度 [3..4]=peer地址2字节 [7..]=payload
            int payloadLen = static_cast<int>(readBuffer_[2]) - 4;
            if (payloadLen < 0)
            {
                std::cerr << "BluetoothDevice postRead failed: invalid payload length." << std::endl;
                len = 0;
                ignoreBuffer(frameLen);
                return -1;
            }

            // 转成内部统一协议:
            // [type][id_len][data_len][id][data]
            //
            // 这里要求 ptr 指向的 buf 至少能装下转换后的内容。
            // 由于 Device::backgroundTask() 传入的是 read() 得到的原始 buf，
            // 这在“扩容后比原始更大”的场景理论上不绝对安全。
            // 但按你现在蓝牙协议，这里输出长度 = readBuffer[2] + 1，和你C版一致。
            data[0] = static_cast<unsigned char>(connectionType_);
            data[1] = 2;                               // id长度固定2
            data[2] = static_cast<unsigned char>(payloadLen);

            std::memcpy(data + 3, readBuffer_ + 3, 2);                // peer地址
            if (payloadLen > 0)
            {
                std::memcpy(data + 5, readBuffer_ + 7,
                            static_cast<std::size_t>(payloadLen));     // 数据
            }

            len = static_cast<int>(readBuffer_[2]) + 1; // 完全照搬你C版
            ignoreBuffer(frameLen);
            return 0;
        }
    }

    // 没找到有效帧
    len = 0;
    return 0;
}

std::vector<uint8_t> BluetoothDevice::preWrite(const uint8_t* data, size_t len)
{
    if (data == nullptr || len < 3)
    {
        std::cerr << "BluetoothDevice preWrite failed: invalid input." << std::endl;
        return {};
    }

    // 内部统一协议:
    // [type][id_len][data_len][id][data]

    int type = static_cast<int>(data[0]);
    if (type != static_cast<int>(Message::ConnectionType::BLE_MESH))
    {
        std::cerr << "BluetoothDevice preWrite failed: connection type mismatch." << std::endl;
        return {};
    }

    int idLen = static_cast<int>(data[1]);
    if (idLen != 2)
    {
        std::cerr << "BluetoothDevice preWrite failed: id length must be 2." << std::endl;
        return {};
    }

    int dataLen = static_cast<int>(data[2]);
    size_t totalInternalLen = 3 + static_cast<size_t>(idLen) + static_cast<size_t>(dataLen);
    if (len < totalInternalLen)
    {
        std::cerr << "BluetoothDevice preWrite failed: incomplete internal packet." << std::endl;
        return {};
    }

    // 目标格式:
    // "AT+MESH" + 2字节id + payload + "\r\n"
    std::vector<uint8_t> out;
    out.reserve(8 + 2 + static_cast<size_t>(dataLen) + 2);

    const char prefix[] = "AT+MESH";
    out.insert(out.end(), prefix, prefix + 8);

    // id
    out.insert(out.end(), data + 3, data + 3 + 2);

    // payload
    if (dataLen > 0)
    {
        out.insert(out.end(), data + 5, data + 5 + dataLen);
    }

    out.push_back('\r');
    out.push_back('\n');

    return out;
}
