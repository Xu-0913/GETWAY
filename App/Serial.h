#pragma once
#include "/home/xu/GetWay/Public/_public.h"
#include "Device.h"

class Serial
{    
    public:
        enum class BaudRate { BR9600, BR115200 };
        enum class StopBits { One, Two };
        enum class Parity { None, Odd, Even };

        struct Config
        {
            BaudRate baudRate{BaudRate::BR9600};
            StopBits stopBits{StopBits::One};
            Parity parity{Parity::None};
            bool blocking{false};  //  Linux串口特有 超时模式
        };

    private:
        Device& device_;
        Config config_;

    public:
        explicit Serial(Device& device);
        ~Serial() = default;

        bool reconfigure(const Config& config);
        bool flush();
        const Config& config() const noexcept { return config_; }
        bool applyConfig();
        bool applyBaudRate(termios& options) const;
        bool applyStopBits(termios& options) const;
        bool applyParity(termios& options) const;
        bool applyBlockingMode(termios& options) const;
        static speed_t toNativeBaudRate(BaudRate baudRate);

};