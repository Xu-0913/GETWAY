#include "Serial.h"
/*options.c_cflag &= ~CSIZE;   // 清空数据位
options.c_cflag |= CS8;      // 8位数据位
options.c_cflag &= ~PARENB;  // 无校验
options.c_cflag &= ~CSTOPB;  // 1位停止位
*/


Serial::Serial(Device& device)
    : device_(device)
{
    if (!applyConfig())
    {
        throw std::runtime_error("Serial applyConfig failed.");
    }
}

bool Serial::reconfigure(const Config& config)
{
    config_ = config;
    return applyConfig();
}

bool Serial::flush()
{
    if (device_.fd() < 0)
    {
        std::cerr << "Serial flush failed: invalid fd." << std::endl;
        return false;
    }

    if (::tcflush(device_.fd(), TCIOFLUSH) != 0)  // 清空串口硬件缓冲区
    {
        std::cerr << "Serial flush failed: " << std::strerror(errno) << std::endl;
        return false;
    }

    return true;
}

bool Serial::applyConfig()
{
    if (device_.fd() < 0)
    {
        std::cerr << "Serial applyConfig failed: invalid fd." << std::endl;
        return false;
    }

    struct termios options;
    std::memset(&options, 0, sizeof(options));
    if (::tcgetattr(device_.fd(), &options) != 0)  // 读取串口当前的硬件配置
    {
        std::cerr << "Serial tcgetattr failed: " << std::strerror(errno) << std::endl;
        return false;
    }

    ::cfmakeraw(&options);  // 串口收发二进制数据必须开！
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;

    if (!applyBaudRate(options)){return false;}
    if (!applyParity(options)){return false;}
    if (!applyStopBits(options)){return false;}
    if (!applyBlockingMode(options)){return false;}

    if (::tcsetattr(device_.fd(), TCSAFLUSH, &options) != 0)
    {
        std::cerr << "Serial tcsetattr failed: " << std::strerror(errno) << std::endl;
        return false;
    }

    return flush();
}

bool Serial::applyBaudRate(termios& options) const
{
    speed_t speed = toNativeBaudRate(config_.baudRate);

    if (::cfsetispeed(&options, speed) != 0)  // 设置输入 输出波特率
    {
        std::cerr << "Serial cfsetispeed failed: " << std::strerror(errno) << std::endl;
        return false;
    }

    if (::cfsetospeed(&options, speed) != 0)
    {
        std::cerr << "Serial cfsetospeed failed: " << std::strerror(errno) << std::endl;
        return false;
    }

    return true;
}

bool Serial::applyStopBits(termios& options) const
{
    options.c_cflag &= ~CSTOPB;

    if (config_.stopBits == StopBits::Two)
    {
        options.c_cflag |= CSTOPB;
    }

    return true;
}

bool Serial::applyParity(termios& options) const
{
    options.c_cflag &= ~(PARENB | PARODD);

    switch (config_.parity)
    {
        case Parity::None:
            break;

        case Parity::Odd:
            options.c_cflag |= (PARENB | PARODD);
            break;

        case Parity::Even:
            options.c_cflag |= PARENB;
            break;

        default:
            std::cerr << "Serial applyParity failed: invalid parity." << std::endl;
            return false;
    }

    return true;
}

bool Serial::applyBlockingMode(termios& options) const
{
    if (config_.blocking)
    {
        // 阻塞模式：至少读到1字节才返回
        options.c_cc[VTIME] = 0;
        options.c_cc[VMIN] = 1;
    }
    else
    {
        // 非阻塞/超时模式：最多等待0.5秒
        options.c_cc[VTIME] = 5;
        options.c_cc[VMIN] = 0;
    }

    return true;
}

speed_t Serial::toNativeBaudRate(BaudRate baudRate)
{
    switch (baudRate)
    {
        case BaudRate::BR9600:
            return B9600;
        case BaudRate::BR115200:
            return B115200;
        default:
            return B9600;
    }
}

/*

Serial::Config cfg;
cfg.baudRate = Serial::BaudRate::BR115200;
cfg.stopBits = Serial::StopBits::One;
cfg.parity = Serial::Parity::None;
cfg.blocking = false;

serial.reconfigure(cfg);

*/
