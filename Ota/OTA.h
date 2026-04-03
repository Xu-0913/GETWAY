#pragma once
#include "/home/xu/GetWay/Public/_public.h"


class OTA
{
public:
    struct Version
    {
        int major{0};
        int minor{0};
        int patch{0};
    };

    enum class Slot
    {
        A = 0,
        B = 1
    };

    enum class State
    {
        IDLE = 0,
        INIT,
        HANDLE_PENDING_BOOT,
        CHECK_VERSION,
        DOWNLOAD_MANIFEST,
        DOWNLOAD_FIRMWARE,
        VERIFY_FIRMWARE,
        WRITE_STANDBY,
        SET_BOOT_TARGET,
        REBOOT_PENDING,
        HEALTH_CHECK,
        COMMIT_SUCCESS,
        ROLLBACK,
        FAILED
    };

    struct Manifest
    {
        Version version{};
        std::string url{};
        std::string sha256{};
        std::size_t size{0};
        bool force{false};
    };

    struct BootMeta
    {
        Slot activeSlot{Slot::A};       // 当前已确认成功的槽位
        Slot pendingSlot{Slot::A};      // 下次准备启动的槽位
        bool upgradePending{false};     // 是否存在待确认升级
        bool bootSuccess{true};         // 当前启动是否已确认成功
        int retryCount{0};              // 新槽位已尝试启动次数
        Version activeVersion{};        // 当前已确认版本
        Version pendingVersion{};       // 待确认版本
    };

    struct Config
    {
        std::string manifestUrl;        // manifest 地址
        std::string downloadFile;       // 临时下载文件（建议 .tmp）
        std::string slotAPath;          // A 槽位文件路径
        std::string slotBPath;          // B 槽位文件路径
        std::string bootMetaPath;       // boot meta 存储路径
        std::string versionFilePath;    // 当前确认版本持久化文件（可选）
        Version currentVersion{};       // 当前运行版本（启动时加载）
        int maxRetryCount{3};           // 新槽位最大重试次数
        int checkIntervalSec{3600};     // 检查更新周期
        long connectTimeoutSec{5};      // HTTP 连接超时
        long downloadTimeoutSec{30};    // HTTP 下载超时
        bool autoReboot{true};          // 升级完成后是否自动重启
        bool strictManifestCheck{true}; // manifest 字段是否严格校验
    };

    private:
        mutable std::mutex mutex_;

        Config config_{};
        State state_{State::IDLE};
        Manifest manifest_{};
        BootMeta bootMeta_{};

        std::atomic<bool> running_{false};
        bool initialized_{false};
        bool curlInitialized_{false};

        std::function<int()> healthCheckCallback_{};
        std::function<int(Slot)> setBootTargetCallback_{};
        std::function<int()> rebootCallback_{};
        std::function<int(const Version&)> persistVersionCallback_{};
public:
    explicit OTA(const Config& config);
    ~OTA();

    OTA(const OTA&) = delete;
    OTA& operator=(const OTA&) = delete;

public:
    // 生命周期
    int init();                 // 初始化：curl / boot meta / version
    int run();                  // 周期运行：持续检查更新
    int runOnce();              // 单次检查更新，方便测试/守护进程调度
    void stop() noexcept;       // 停止 run() 循环

    // 启动确认流程
    int handlePendingBoot();    // 进程启动后尽早调用：处理待确认升级、递增重试、必要时回滚
    int commit();               // 健康检查成功后确认新版本
    int rollback();             // 健康检查失败或启动异常时主动回滚

    // 状态查询
    State getState() const noexcept;
    Version getCurrentVersion() const noexcept;
    Manifest getManifest() const;
    BootMeta getBootMeta() const;
    bool isUpgradePending() const noexcept;

    // 回调注册
    void registerHealthCheckCallback(std::function<int()> cb);
    void registerSetBootTargetCallback(std::function<int(Slot)> cb);
    void registerRebootCallback(std::function<int()> cb);
    void registerPersistVersionCallback(std::function<int(const Version&)> cb);

private:
    // 主流程步骤
    int stepCheckVersion();
    int stepDownloadManifest();
    int stepDownloadFirmware();
    int stepVerifyFirmware();
    int stepWriteStandby();
    int stepSetBootTarget();
    int stepReboot();
    int stepHealthCheck();

    // 启动确认相关
    int stepHandlePendingBoot();
    int stepCommitSuccess();
    int stepRollback();

    // 工具函数
    bool isNewerVersion(const Version& remote, const Version& local) const;
    bool isSameVersion(const Version& lhs, const Version& rhs) const;
    bool isValidVersion(const Version& version) const;
    bool isValidSha256(const std::string& sha256) const;

    Slot getStandbySlot() const;
    std::string getStandbyPath() const;
    std::string getActivePath() const;
    const char* slotToString(Slot slot) const noexcept;
    std::string versionToString(const Version& version) const;

    // HTTP / 解析
    int httpGetString(const std::string& url, std::string& response);
    int httpDownloadFile(const std::string& url, const std::string& filePath);
    int parseManifest(const std::string& jsonText, Manifest& manifest);
    int validateManifest(const Manifest& manifest) const;

    // 校验 / 文件工具
    int calcFileSha256(const std::string& filePath, std::string& sha256);
    int getFileSize(const std::string& filePath, std::size_t& size);
    int removeFileIfExists(const std::string& filePath);
    int syncFile(const std::string& filePath);
    int atomicReplaceFile(const std::string& srcFile, const std::string& dstFile);

    // Version 持久化
    int loadCurrentVersion();
    int saveCurrentVersion(const Version& version);

    // Boot Meta 持久化
    int loadBootMeta();
    int saveBootMeta() const;
    int resetBootMetaToDefault();

    // 槽位管理
    int writeToStandbySlot(const std::string& srcFile, const std::string& dstFile);
    int setBootTarget(Slot slot);

    // 健康检查 / 重启
    int healthCheck();
    int rebootSystem();

    // curl 回调
    static std::size_t writeToString(void* contents, std::size_t size, std::size_t nmemb, void* userp);
    static std::size_t writeToFile(void* contents, std::size_t size, std::size_t nmemb, void* userp);


};