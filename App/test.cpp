#include "run.h"
#include <iostream>
#include <thread>
#include <chrono>

int main()
{
    std::thread t([]()
    {
        int ret = runGateway();
        std::cout << "runGateway exit, ret = " << ret << std::endl;
    });

    std::this_thread::sleep_for(std::chrono::seconds(10));

    std::cout << "request stop gateway..." << std::endl;
    stopGateway();

    t.join();
    return 0;
}