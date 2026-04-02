#include "Buffer.h"

Buffer::Buffer(std::size_t size): buffer_(size), size_(size), start_(0), len_(0)
{

}

std::size_t Buffer::read(char* dst, std::size_t len)
{
    if (dst == nullptr || len == 0) return 0;

    std::lock_guard<std::mutex> lock(mutex_);

    std::size_t read_len = std::min(len, len_);  // 防止多读
    if (read_len == 0) return 0;

    if (start_ + read_len <= size_)
    {
        memcpy(dst, buffer_.data() + start_, read_len);
        start_ += read_len;
        if (start_ == size_) start_ = 0;
    }
    else
    {
        std::size_t first_len = size_ - start_;
        std::size_t second_len = read_len - first_len;

        memcpy(dst, buffer_.data() + start_, first_len);
        memcpy(dst + first_len, buffer_.data(), second_len);
        start_ = second_len;
    }

    len_ -= read_len;
    if (len_ == 0) start_ = 0;  // 不一定每次都从0开始start_ = 20 len_ = 10 read_len = 10
    return read_len;
}

bool Buffer::write(const char* src, std::size_t len)
{
    if (src == nullptr || len == 0) return true;

    std::lock_guard<std::mutex> lock(mutex_);

    if (len > size_ - len_) return false;

    std::size_t write_pos = (start_ + len_) % size_;

    if (write_pos + len <= size_)
    {
        memcpy(buffer_.data() + write_pos, src, len);
    }
    else
    {
        std::size_t first_len = size_ - write_pos;
        std::size_t second_len = len - first_len;

        memcpy(buffer_.data() + write_pos, src, first_len);
        memcpy(buffer_.data(), static_cast<const char*>(src) + first_len, second_len);
    }

    len_ += len;
    return true;
}

void Buffer::clear()
{
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        start_ = 0;
        len_ = 0;
    }
}

std::size_t Buffer::readableSize() const
{
    return len_;
}

std::size_t Buffer::writableSize() const
{
    return size_ - len_;
}

std::size_t Buffer::capacity() const
{
    return size_;
}

bool Buffer::empty() const
{
    return len_ == 0;
}

bool Buffer::full() const
{
    return len_ == size_;
}

bool Buffer::peek(char* dst, std::size_t len) const
{
    if (len_ < len) return false;
    size_t firstPart = std::min(len, buffer_.size() - start_);
    std::memcpy(dst, &buffer_[start_], firstPart);
    if (len > firstPart) std::memcpy(dst + firstPart, &buffer_[0], len - firstPart);
    return true;
}


int main()
{
    Buffer buf(1024);

    const char* msg = "hello";
    buf.write(msg, 5);

    char tmp[16] = {0};
    std::size_t n = buf.read(tmp, 5);

    std::cout << "read bytes: " << n << std::endl;
    std::cout << "data: " << tmp << std::endl;

    return 0;
}



