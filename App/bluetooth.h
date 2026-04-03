#pragma once
#include "Device.h"

class BluetoothDevice : public Device
{
private:
    static constexpr std::size_t READ_BUFFER_CAPACITY = 128;
    unsigned char readBuffer_[READ_BUFFER_CAPACITY]{0};
    int readBufferLen_{0};

    static constexpr unsigned char FIX_HEADER_0 = 0xF1;
    static constexpr unsigned char FIX_HEADER_1 = 0xDD;

private:
    void ignoreBuffer(int n);

public:
    BluetoothDevice(const std::string& filename,
                    ThreadPool& threadPool,
                    std::size_t bufferSize = 16384);

    virtual ~BluetoothDevice() = default;

protected:
    int postRead(void* ptr, int& len) override;
    std::vector<uint8_t> preWrite(const uint8_t* data, size_t len) override;
};