#include "OTA.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>

namespace
{
    std::atomic<bool> g_running{true};

    void signalHandler(int)
    {
        g_running = false;
    }

    void installSignals()
    {
        std::signal(SIGINT, signalHandler);
        std::signal(SIGTERM, signalHandler);
    }
}

int main()
{
    installSignals();

    OTA::Config cfg;
    cfg.manifestUrl = "http://127.0.0.1:8080/manifest.json";
    cfg.downloadFile = "/tmp/firmware.bin";
    cfg.slotAPath = "/tmp/slot_a.bin";
    cfg.slotBPath = "/tmp/slot_b.bin";
    cfg.bootMetaPath = "/tmp/boot_meta.json";
    cfg.versionFilePath = "/tmp/version.json";

    cfg.currentVersion = {1, 0, 0};
    cfg.maxRetryCount = 3;
    cfg.checkIntervalSec = 3600;
    cfg.connectTimeoutSec = 5;
    cfg.downloadTimeoutSec = 30;
    cfg.autoReboot = false;          // 调试阶段先别自动重启
    cfg.strictManifestCheck = true;

    try
    {
        OTA ota(cfg);

        ota.registerHealthCheckCallback([]() -> int
        {
            std::cout << "[OTA_MAIN] health check pass" << std::endl;
            return 0;
        });

        ota.registerSetBootTargetCallback([](OTA::Slot slot) -> int
        {
            std::cout << "[OTA_MAIN] set boot target to slot "
                      << ((slot == OTA::Slot::A) ? "A" : "B") << std::endl;
            return 0;
        });

        ota.registerRebootCallback([]() -> int
        {
            std::cout << "[OTA_MAIN] reboot requested" << std::endl;
            return 0;
        });

        ota.registerPersistVersionCallback([](const OTA::Version& v) -> int
        {
            std::cout << "[OTA_MAIN] persist version: "
                      << v.major << "." << v.minor << "." << v.patch << std::endl;
            return 0;
        });

        if (ota.init() != 0)
        {
            std::cerr << "ota init failed." << std::endl;
            return -1;
        }

        if (ota.handlePendingBoot() != 0)
        {
            std::cerr << "ota handlePendingBoot failed." << std::endl;
            return -1;
        }

        if (ota.isUpgradePending())
        {
            if (ota.commit() != 0)
            {
                std::cerr << "ota commit failed." << std::endl;
                return -1;
            }
        }

        while (g_running)
        {
            if (ota.runOnce() != 0)
            {
                std::cerr << "ota runOnce failed." << std::endl;
            }

            for (int i = 0; i < cfg.checkIntervalSec && g_running; ++i)
            {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }

        ota.stop();
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "ota exception: " << e.what() << std::endl;
        return -1;
    }
    catch (...)
    {
        std::cerr << "ota exception: unknown" << std::endl;
        return -1;
    }
}