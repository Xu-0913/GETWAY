#pragma once
#include "/home/xu/GetWay/Public/_public.h"
#include "Buffer.h"
#include "Message.h"
#include "ThreadPool.h"

class Device
{

    protected:
        std::string filename_;                   // 设备文件名
        int fd_;                                 // 设备文件描述符
        std::thread backgroundThread_;           // 后台线程
        Message::ConnectionType connectionType_; // 连接类型
        Buffer recvBuffer_;                  
        Buffer sendBuffer_;           
        ThreadPool& threadPool_;            
        std::atomic<bool> isRunning_;       
        std::function<int(void*, int)> recvCallback_;          
        mutable std::mutex callbackMutex_;   
    
    public:
        // 构造析构
        Device(const std::string& filename,Message::ConnectionType connectionType,ThreadPool& threadPool,std::size_t bufferSize = 16384);
        virtual ~Device();

        bool start();                              //  启动设备后台线程
        bool write(const void* ptr, int len);     // 向设备写入数据
        void stop();                            // 停止后台线程
        void close();                           // 关闭设备、释放资源
        const std::string& filename() const;   // 获取设备路径
        int fd() const;                        // 获取设备文件描述符
        bool isRunning() const;                // 获取设备运行状态
        Message::ConnectionType connectionType() const; // 获取连接类型
        
        // 虚函数
    protected:    
        virtual void backgroundTask();                // 后台线程核心函数
        virtual void recvTask();                       // 接收任务处理函数   
        virtual void sendTask();                      // 发送任务处理函数
        virtual int postRead(void* ptr, int& len);  // 读数据后处理函数
        virtual int preWrite(void* ptr, int& len);  // 对应 vptr->pre_write 写数据前处理函数
        
    public:
        //注册回调函数入口
        void registerRecvCallback(std::function<int(void*, int)> cb);   
    
};