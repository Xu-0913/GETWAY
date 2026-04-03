#include "ThreadPool.h"

ThreadPool::ThreadPool(size_t threadnum,const std::string& type):stop_(false), th_type(type)
{
    // 启动threadnum个线程，每个线程将阻塞在条件变量上。
	for (size_t ii = 0; ii < threadnum; ii++)
    {
        // 用lambda函创建线程。
		threads_.emplace_back([this]
		{
			printf("create thread(%ld).\n", syscall(SYS_gettid));
			while (true)
			{
				std::function<void()> task;
				{
					std::unique_lock<std::mutex> lock(this->mutex_);
					this->condition_.wait(lock, [this]{return this->stop_ || !this->taskqueue_.empty();});  // wait(lock,predicate) predicate:一个返回 bool 的函数
					if (this->stop_ && this->taskqueue_.empty()) return;// 线程停 + 队列也空了才真正退出
					task = std::move(this->taskqueue_.front());
					this->taskqueue_.pop();
				}
				printf("thread is %ld.\n", syscall(SYS_gettid));
				task();
			}
		});
    }
}

void ThreadPool::addtask(std::function<void()> task)
{
    stop_ = true;
    condition_.notify_all();

    for (auto& x : threads_)
    {
        if (x.joinable())
        {
            x.join();
        }
    }
}

void ThreadPool::stopthread()
{
	stop_ = true;
	condition_.notify_all();  // 唤醒全部的线程。
	for (auto& x: threads_) // 等待全部线程执行完任务后退出。
        x.join();
}

ThreadPool::~ThreadPool()
{
	stopthread();
}
 
size_t ThreadPool::size()
{
	return threads_.size();
}

// int main()
// {
//     const size_t thread_num = 5;
//     const size_t task_num   = 20000;

//     std::atomic<size_t> counter = 0;

//     auto start = std::chrono::steady_clock::now();

//     {
//         ThreadPool pool(thread_num, "gateway");

//         for (size_t i = 0; i < task_num; ++i)
//         {
//             pool.addtask([&counter, i]{
//                 // 模拟轻量业务处理
//                 volatile int x = 0;
//                 for (int k = 0; k < 100; ++k) x += k;

//                 counter.fetch_add(1, std::memory_order_relaxed);
//             });
//         }
//     }

// }
