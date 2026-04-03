#include "Deamon.h"
#include <iostream>

int main()
{
    DaemonManager manager;

    // 调试阶段：先前台运行，日志直接打终端
    manager.setDaemonMode(false);
    manager.setRedirectStdIO(false);

    // 部署阶段可改成：
    // manager.setDaemonMode(true);
    // manager.setRedirectStdIO(true);
    // manager.setStdoutFile("/tmp/getway_daemon.out");
    // manager.setStderrFile("/tmp/getway_daemon.err");

    manager.setCheckIntervalMs(200);
    manager.setRestartDelaySec(1);
    manager.setStopTimeoutSec(5);
    manager.setMaxCrashCount(20);

    // 先守护网关主进程
    if (manager.addProcess("gateway", "/home/xu/GetWay/bin/GETWAY", {}, true, 10) != 0)
    {
        std::cerr << "main failed: add gateway process failed." << std::endl;
        return -1;
    }

    // OTA 先不纳入守护，后面你更新好了再打开
    /*
    if (manager.addProcess("ota", "/home/xu/GetWay/bin/ota", {}, true, 10) != 0)
    {
        std::cerr << "main failed: add ota process failed." << std::endl;
        return -1;
    }
    */

    return manager.run();
}