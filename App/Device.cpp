#include "Device.h"
constexpr std::size_t READ_CHUNK_SIZE = 1024;

Device::Device(const std::string& filename,Message::ConnectionType connectionType,ThreadPool& threadPool,std::size_t bufferSize): filename_(filename),
                                                                                                                                  fd_(-1),connectionType_(connectionType),recvBuffer_(bufferSize),
                                                                                                                                  sendBuffer_(bufferSize),threadPool_(threadPool),isRunning_(false)
{
    fd_ = open(filename_.c_str(), O_RDWR | O_NOCTTY);  // 可读可写、不占用控制台
    if (fd_ < 0){std::cerr << "Device open failed: " << filename_ << std::endl;throw std::runtime_error("Device open failed");}
    std::cout << "Device initialized: " << filename_ << std::endl;
}

Device::~Device()
{
    close();
}

bool Device::start()
{
    if (isRunning_){return false;}
    if (fd_ < 0){std::cerr << "Device start failed: invalid fd." << std::endl;return false;}
    isRunning_ = true;

    try
    {
        backgroundThread_ = std::thread([this](){this->backgroundTask();});  // vector.emplace_back[](){}
    }

    catch (const std::exception& e)
    {
        isRunning_ = false;
        std::cerr << "Create background thread failed: " << e.what() << std::endl;
        return false;
    }

    catch (...)
    {
        isRunning_ = false;
        std::cerr << "Create background thread failed: unknown exception" << std::endl;
        return false;
    }

    std::cout << "Device started: " << filename_ << std::endl;
    return 0;
}

bool Device::write(const void* ptr, int len)
{
    if (ptr == nullptr || len <= 0) {return false;}
    if (!isRunning_){std::cerr << "Device write failed: device not running." << std::endl;return false;}
    if (!sendBuffer_.write(static_cast<const char*>(ptr), static_cast<std::size_t>(len))) {std::cerr << "Device write failed: send buffer full." << std::endl;return false;}
    threadPool_.addtask([this](){this->sendTask();});
    return true;
}

void Device::stop()
{
    if (!isRunning_) {return;}
    isRunning_ = false;

    if (fd_ >= 0){::close(fd_);fd_ = -1;}  // 关文件
    if (backgroundThread_.joinable()) {backgroundThread_.join();}  // 关线程
    
    std::cout << "Device stopped: " << filename_ << std::endl;
}

void Device::close()
{
    stop();
    recvBuffer_.clear();
    sendBuffer_.clear();
}

void Device::registerRecvCallback(std::function<int(void*, int)> cb)
{
    std::lock_guard<std::mutex> lock(callbackMutex_);
    recvCallback_ = std::move(cb);
}

const std::string& Device::filename() const
{
    return filename_;
}

int Device::fd() const
{
    return fd_;
}

bool Device::isRunning() const
{
    return isRunning_;
}

Message::ConnectionType Device::connectionType() const
{
    return connectionType_;
}

// 以下是默认虚函数实现
void Device::backgroundTask()
{
    std::vector<unsigned char> buf(READ_CHUNK_SIZE);
    while (isRunning_)
    {
        if (fd_ < 0){break;}
        ssize_t n = ::read(fd_, buf.data(), buf.size());

        if (n > 0)
        {
            int len = static_cast<int>(n);
            if (postRead(buf.data(), len) < 0)
            {
                std::cerr << "postRead failed, data discarded." << std::endl;
                continue;
            }

            if (!recvBuffer_.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::size_t>(len)))
            {
                std::cerr << "recvBuffer write failed, data discarded." << std::endl;
                continue;
            }
            threadPool_.addtask([this](){this->recvTask();});
            continue;
        }

        if (n < 0)
        {
            // 被信号中断 重试
            if (errno == EINTR){continue;}
            if (!isRunning_ || fd_ < 0){break;}
            std::cerr << "Read device error: " << std::strerror(errno) << std::endl;
            ::usleep(10000);
            continue;
        }
        // n == 0
        std::cerr << "Read returned 0, device may be closed." << std::endl;
        break;
    }
}

void Device::recvTask()
{
    // 线程安全获取回调
    std::function<int(void*, int)> callback; {std::lock_guard<std::mutex> lock(callbackMutex_);callback = recvCallback_;}
    if (!callback){std::cerr << "Recv callback not registered." << std::endl;return;}

    while (true)
    {
        
        if (recvBuffer_.readableSize() < 3){return;}
        unsigned char header[3] = {0};
        if (!recvBuffer_.peek(reinterpret_cast<char*>(header), 3)) {return;}
        int bodyLen = static_cast<int>(header[1]) + static_cast<int>(header[2]);
        std::size_t totalLen = 3 + static_cast<std::size_t>(bodyLen);

        // 半包
        if (recvBuffer_.readableSize() < totalLen) {return;}

        // 整包整
        std::vector<unsigned char> fullPacket(totalLen, 0);  // 缓冲是环形 不能直接放进接口 不然要写处理函数
        std::size_t n = recvBuffer_.read(reinterpret_cast<char*>(fullPacket.data()), totalLen);
        if (n != totalLen)
        {
            std::cerr << "recvBuffer read packet failed." << std::endl;
            return;
        }

        // 执行业务回调
        if (callback(fullPacket.data(), static_cast<int>(fullPacket.size())) < 0)  // 缓冲内有效数据的vector首地址，vector长度
        {
            std::cerr << "Recv callback process failed, packet discarded." << std::endl;
        }
    }
}

void Device::sendTask()
{
    while (true)
    {
        if (sendBuffer_.readableSize() < 3) {return;}

        unsigned char header[3] = {0};
        if (!sendBuffer_.peek(reinterpret_cast<char*>(header), 3)){return;}

        // 计算整包长度
        int bodyLen = static_cast<int>(header[1]) + static_cast<int>(header[2]);
        std::size_t totalLen = 3 + static_cast<std::size_t>(bodyLen);

        // 半包
        if (sendBuffer_.readableSize() < totalLen){return;}

        // 整包
        std::vector<unsigned char> packet(totalLen, 0);
        std::size_t n = sendBuffer_.read(reinterpret_cast<char*>(packet.data()), totalLen);
        if (n != totalLen)
        {
            std::cerr << "sendBuffer read packet failed." << std::endl;
            return;
        }

        // 6. 写前处理，允许修改长度
        int writeLen = static_cast<int>(packet.size());
        if (preWrite(packet.data(), writeLen) < 0) {std::cerr << "preWrite failed, packet discarded." << std::endl;continue;}

        // 7. fd 无效，当前包丢弃
        if (fd_ < 0){std::cerr << "Send failed: invalid fd." << std::endl;return;}

        // 写设备
        ssize_t ret = ::write(fd_, packet.data(), static_cast<std::size_t>(writeLen));
        if (ret < 0) {std::cerr << "Write device data error." << std::endl;return;}
    }
}

int Device::postRead(void* ptr, int& len)
{
    (void)ptr;
    (void)len;
    return 0;
}

int Device::preWrite(void* ptr, int& len)
{
    (void)ptr;
    (void)len;
    return 0;
}