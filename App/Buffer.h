#pragma once
#include "/home/xu/GetWay/Public/_public.h"


class Buffer
{
    private:
        std::vector<char> buffer_;
        std::size_t size_;  // 总长度  
        std::size_t start_;  // 起始pos  
        std::size_t len_;   //  使用长度
        // idc::clogfile log_;  // 启用日志
        std::mutex mutex_;  // const 函数需要mutable放行锁 

    public:
        Buffer(size_t size);
        ~Buffer() = default;

        // 禁止拷贝赋值
        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;

    public:
        std::size_t read(char* dst, std::size_t len);
        bool write(const char* src, std::size_t len);
        bool peek(char* dst, std::size_t len) const;
        void clear();
        std::size_t readableSize() const;
        std::size_t writableSize() const;
        std::size_t capacity() const;
        bool empty() const;
        bool full() const;
};







