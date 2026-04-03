#include "OTA.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <algorithm>
#include <cctype>

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>


#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace
{
    std::string toLowerCopy(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c)
                       {
                           return static_cast<char>(std::tolower(c));
                       });
        return s;
    }

    json versionToJson(const OTA::Version& v)
    {
        json j;
        j["major"] = v.major;
        j["minor"] = v.minor;
        j["patch"] = v.patch;
        return j;
    }

    OTA::Version versionFromJson(const json& j)
    {
        OTA::Version v;
        v.major = j.at("major").get<int>();
        v.minor = j.at("minor").get<int>();
        v.patch = j.at("patch").get<int>();
        return v;
    }

    int syncSingleFile(const std::string& filePath)
    {
        int fd = ::open(filePath.c_str(), O_RDONLY);
        if (fd < 0)
        {
            return -1;
        }

        int ret = (::fsync(fd) == 0) ? 0 : -1;
        ::close(fd);
        return ret;
    }

    int atomicReplaceByRename(const std::string& srcFile, const std::string& dstFile)
    {
        if (::rename(srcFile.c_str(), dstFile.c_str()) != 0)
        {
            return -1;
        }

        return syncSingleFile(dstFile);
    }
}

// ========================= ctor / dtor =========================

OTA::OTA(const Config& config)
    : config_(config)
{
}

OTA::~OTA()
{
    stop();

    if (curlInitialized_)
    {
        curl_global_cleanup();
        curlInitialized_ = false;
    }
}

// ========================= lifecycle =========================

int OTA::init()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (initialized_)
    {
        return 0;
    }

    state_ = State::INIT;

    if (!curlInitialized_)
    {
        CURLcode rc = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (rc != CURLE_OK)
        {
            state_ = State::FAILED;
            std::cerr << "[OTA] curl_global_init failed\n";
            return -1;
        }
        curlInitialized_ = true;
    }

    if (config_.maxRetryCount <= 0)
    {
        config_.maxRetryCount = 1;
    }

    if (config_.checkIntervalSec <= 0)
    {
        config_.checkIntervalSec = 3600;
    }

    if (config_.connectTimeoutSec <= 0)
    {
        config_.connectTimeoutSec = 5;
    }

    if (config_.downloadTimeoutSec <= 0)
    {
        config_.downloadTimeoutSec = 30;
    }

    if (loadCurrentVersion() != 0)
    {
        state_ = State::FAILED;
        std::cerr << "[OTA] loadCurrentVersion failed\n";
        return -1;
    }

    if (loadBootMeta() != 0)
    {
        state_ = State::FAILED;
        std::cerr << "[OTA] loadBootMeta failed\n";
        return -1;
    }

    initialized_ = true;
    state_ = State::IDLE;
    std::cout << "[OTA] init success\n";
    return 0;
}

int OTA::run()
{
    if (!initialized_ && init() != 0)
    {
        return -1;
    }

    running_ = true;

    while (running_)
    {
        int ret = runOnce();
        if (ret != 0)
        {
            state_ = State::FAILED;
        }

        for (int i = 0; i < config_.checkIntervalSec && running_; ++i)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    return 0;
}

int OTA::runOnce()
{
    if (!initialized_ && init() != 0)
    {
        return -1;
    }

    manifest_ = Manifest{};

    if (stepCheckVersion() != 0)
    {
        state_ = State::FAILED;
        return -1;
    }

    if (!isNewerVersion(manifest_.version, config_.currentVersion) && !manifest_.force)
    {
        state_ = State::IDLE;
        std::cout << "[OTA] no newer version\n";
        return 0;
    }

    if (stepDownloadFirmware() != 0)
    {
        state_ = State::FAILED;
        return -1;
    }

    if (stepVerifyFirmware() != 0)
    {
        state_ = State::FAILED;
        return -1;
    }

    if (stepWriteStandby() != 0)
    {
        state_ = State::FAILED;
        return -1;
    }

    if (stepSetBootTarget() != 0)
    {
        state_ = State::FAILED;
        return -1;
    }

    state_ = State::REBOOT_PENDING;
    std::cout << "[OTA] upgrade prepared, reboot pending\n";

    if (config_.autoReboot)
    {
        return stepReboot();
    }

    return 0;
}

void OTA::stop() noexcept
{
    running_ = false;
}

// ========================= boot pending / commit / rollback =========================

int OTA::handlePendingBoot()
{
    if (!initialized_ && init() != 0)
    {
        return -1;
    }

    return stepHandlePendingBoot();
}

int OTA::commit()
{
    if (!initialized_ && init() != 0)
    {
        return -1;
    }

    if (!bootMeta_.upgradePending)
    {
        std::cout << "[OTA] commit skipped: no upgrade pending\n";
        return 0;
    }

    if (stepHealthCheck() != 0)
    {
        std::cerr << "[OTA] commit failed: health check failed, rollback start\n";
        return stepRollback();
    }

    return stepCommitSuccess();
}

int OTA::rollback()
{
    if (!initialized_ && init() != 0)
    {
        return -1;
    }

    return stepRollback();
}

// ========================= getters =========================

OTA::State OTA::getState() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

OTA::Version OTA::getCurrentVersion() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return config_.currentVersion;
}

OTA::Manifest OTA::getManifest() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return manifest_;
}

OTA::BootMeta OTA::getBootMeta() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return bootMeta_;
}

bool OTA::isUpgradePending() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return bootMeta_.upgradePending;
}

// ========================= callback register =========================

void OTA::registerHealthCheckCallback(std::function<int()> cb)
{
    std::lock_guard<std::mutex> lock(mutex_);
    healthCheckCallback_ = std::move(cb);
}

void OTA::registerSetBootTargetCallback(std::function<int(Slot)> cb)
{
    std::lock_guard<std::mutex> lock(mutex_);
    setBootTargetCallback_ = std::move(cb);
}

void OTA::registerRebootCallback(std::function<int()> cb)
{
    std::lock_guard<std::mutex> lock(mutex_);
    rebootCallback_ = std::move(cb);
}

void OTA::registerPersistVersionCallback(std::function<int(const Version&)> cb)
{
    std::lock_guard<std::mutex> lock(mutex_);
    persistVersionCallback_ = std::move(cb);
}

// ========================= steps =========================

int OTA::stepCheckVersion()
{
    state_ = State::CHECK_VERSION;
    std::cout << "[OTA] checking version\n";
    return stepDownloadManifest();
}

int OTA::stepDownloadManifest()
{
    state_ = State::DOWNLOAD_MANIFEST;

    std::string response;
    if (httpGetString(config_.manifestUrl, response) != 0)
    {
        std::cerr << "[OTA] download manifest failed\n";
        return -1;
    }

    Manifest parsed;
    if (parseManifest(response, parsed) != 0)
    {
        std::cerr << "[OTA] parse manifest failed\n";
        return -1;
    }

    manifest_ = std::move(parsed);
    std::cout << "[OTA] manifest loaded, version = "
              << versionToString(manifest_.version) << "\n";
    return 0;
}

int OTA::stepDownloadFirmware()
{
    state_ = State::DOWNLOAD_FIRMWARE;

    if (manifest_.url.empty())
    {
        std::cerr << "[OTA] firmware url is empty\n";
        return -1;
    }

    if (removeFileIfExists(config_.downloadFile) != 0)
    {
        std::cerr << "[OTA] remove old download temp file failed\n";
        return -1;
    }

    std::cout << "[OTA] downloading firmware\n";
    return httpDownloadFile(manifest_.url, config_.downloadFile);
}

int OTA::stepVerifyFirmware()
{
    state_ = State::VERIFY_FIRMWARE;

    std::size_t size = 0;
    if (getFileSize(config_.downloadFile, size) != 0)
    {
        std::cerr << "[OTA] get downloaded file size failed\n";
        return -1;
    }

    if (size != manifest_.size)
    {
        std::cerr << "[OTA] firmware size mismatch\n";
        return -1;
    }

    std::string sha256;
    if (calcFileSha256(config_.downloadFile, sha256) != 0)
    {
        std::cerr << "[OTA] calc sha256 failed\n";
        return -1;
    }

    if (toLowerCopy(sha256) != toLowerCopy(manifest_.sha256))
    {
        std::cerr << "[OTA] firmware sha256 mismatch\n";
        return -1;
    }

    std::cout << "[OTA] firmware verify success\n";
    return 0;
}

int OTA::stepWriteStandby()
{
    state_ = State::WRITE_STANDBY;

    const std::string dst = getStandbyPath();
    if (dst.empty())
    {
        std::cerr << "[OTA] standby path is empty\n";
        return -1;
    }

    std::cout << "[OTA] writing firmware to standby slot "
              << slotToString(getStandbySlot()) << "\n";

    return writeToStandbySlot(config_.downloadFile, dst);
}

int OTA::stepSetBootTarget()
{
    state_ = State::SET_BOOT_TARGET;

    BootMeta oldMeta = bootMeta_;

    bootMeta_.pendingSlot = getStandbySlot();
    bootMeta_.upgradePending = true;
    bootMeta_.bootSuccess = false;
    bootMeta_.retryCount = 0;
    bootMeta_.pendingVersion = manifest_.version;

    if (saveBootMeta() != 0)
    {
        bootMeta_ = oldMeta;
        std::cerr << "[OTA] save boot meta failed before switch target\n";
        return -1;
    }

    if (setBootTarget(bootMeta_.pendingSlot) != 0)
    {
        bootMeta_ = oldMeta;
        saveBootMeta();
        std::cerr << "[OTA] set boot target failed\n";
        return -1;
    }

    std::cout << "[OTA] boot target set to slot "
              << slotToString(bootMeta_.pendingSlot) << "\n";
    return 0;
}

int OTA::stepReboot()
{
    state_ = State::REBOOT_PENDING;
    std::cout << "[OTA] reboot start\n";
    return rebootSystem();
}

int OTA::stepHealthCheck()
{
    state_ = State::HEALTH_CHECK;
    return healthCheck();
}

int OTA::stepHandlePendingBoot()
{
    state_ = State::HANDLE_PENDING_BOOT;

    if (!bootMeta_.upgradePending)
    {
        std::cout << "[OTA] no pending boot\n";
        return 0;
    }

    if (bootMeta_.bootSuccess)
    {
        std::cout << "[OTA] pending boot already marked success\n";
        return 0;
    }

    ++bootMeta_.retryCount;

    std::cout << "[OTA] pending boot detected, slot = "
              << slotToString(bootMeta_.pendingSlot)
              << ", retry = " << bootMeta_.retryCount
              << "/" << config_.maxRetryCount << "\n";

    if (bootMeta_.retryCount > config_.maxRetryCount)
    {
        std::cerr << "[OTA] pending boot exceeds max retry count, rollback start\n";
        return stepRollback();
    }

    if (saveBootMeta() != 0)
    {
        std::cerr << "[OTA] save boot meta failed while handling pending boot\n";
        return -1;
    }

    return 0;
}

int OTA::stepCommitSuccess()
{
    state_ = State::COMMIT_SUCCESS;

    bootMeta_.activeSlot = bootMeta_.pendingSlot;
    bootMeta_.activeVersion = bootMeta_.pendingVersion;
    bootMeta_.upgradePending = false;
    bootMeta_.bootSuccess = true;
    bootMeta_.retryCount = 0;

    config_.currentVersion = bootMeta_.activeVersion;

    if (saveCurrentVersion(config_.currentVersion) != 0)
    {
        std::cerr << "[OTA] save current version failed\n";
        return -1;
    }

    if (saveBootMeta() != 0)
    {
        std::cerr << "[OTA] save boot meta failed on commit\n";
        return -1;
    }

    std::cout << "[OTA] commit success, active slot = "
              << slotToString(bootMeta_.activeSlot)
              << ", version = " << versionToString(config_.currentVersion) << "\n";
    return 0;
}

int OTA::stepRollback()
{
    state_ = State::ROLLBACK;

    BootMeta oldMeta = bootMeta_;

    bootMeta_.pendingSlot = bootMeta_.activeSlot;
    bootMeta_.pendingVersion = bootMeta_.activeVersion;
    bootMeta_.upgradePending = false;
    bootMeta_.bootSuccess = true;
    bootMeta_.retryCount = 0;

    if (saveBootMeta() != 0)
    {
        bootMeta_ = oldMeta;
        std::cerr << "[OTA] save boot meta failed on rollback\n";
        return -1;
    }

    if (setBootTarget(bootMeta_.activeSlot) != 0)
    {
        bootMeta_ = oldMeta;
        saveBootMeta();
        std::cerr << "[OTA] set boot target failed on rollback\n";
        return -1;
    }

    std::cout << "[OTA] rollback prepared to slot "
              << slotToString(bootMeta_.activeSlot) << "\n";

    if (config_.autoReboot)
    {
        return stepReboot();
    }

    state_ = State::REBOOT_PENDING;
    return 0;
}

// ========================= utility =========================

bool OTA::isNewerVersion(const Version& remote, const Version& local) const
{
    if (remote.major != local.major) return remote.major > local.major;
    if (remote.minor != local.minor) return remote.minor > local.minor;
    return remote.patch > local.patch;
}

bool OTA::isSameVersion(const Version& lhs, const Version& rhs) const
{
    return lhs.major == rhs.major &&
           lhs.minor == rhs.minor &&
           lhs.patch == rhs.patch;
}

bool OTA::isValidVersion(const Version& version) const
{
    return version.major >= 0 &&
           version.minor >= 0 &&
           version.patch >= 0;
}

bool OTA::isValidSha256(const std::string& sha256) const
{
    if (sha256.size() != 64)
    {
        return false;
    }

    for (unsigned char c : sha256)
    {
        if (!std::isxdigit(c))
        {
            return false;
        }
    }

    return true;
}

OTA::Slot OTA::getStandbySlot() const
{
    return (bootMeta_.activeSlot == Slot::A) ? Slot::B : Slot::A;
}

std::string OTA::getStandbyPath() const
{
    return (getStandbySlot() == Slot::A) ? config_.slotAPath : config_.slotBPath;
}

std::string OTA::getActivePath() const
{
    return (bootMeta_.activeSlot == Slot::A) ? config_.slotAPath : config_.slotBPath;
}

const char* OTA::slotToString(Slot slot) const noexcept
{
    return (slot == Slot::A) ? "A" : "B";
}

std::string OTA::versionToString(const Version& version) const
{
    return std::to_string(version.major) + "." +
           std::to_string(version.minor) + "." +
           std::to_string(version.patch);
}

// ========================= HTTP / manifest =========================

int OTA::httpGetString(const std::string& url, std::string& response)
{
    response.clear();

    CURL* curl = curl_easy_init();
    if (curl == nullptr)
    {
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &OTA::writeToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, config_.connectTimeoutSec);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, config_.downloadTimeoutSec);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode rc = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    return (rc == CURLE_OK) ? 0 : -1;
}

int OTA::httpDownloadFile(const std::string& url, const std::string& filePath)
{
    FILE* fp = std::fopen(filePath.c_str(), "wb");
    if (fp == nullptr)
    {
        return -1;
    }

    CURL* curl = curl_easy_init();
    if (curl == nullptr)
    {
        std::fclose(fp);
        removeFileIfExists(filePath);
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &OTA::writeToFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, config_.connectTimeoutSec);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, config_.downloadTimeoutSec);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode rc = curl_easy_perform(curl);

    std::fflush(fp);
    int fd = ::fileno(fp);
    if (fd >= 0)
    {
        ::fsync(fd);
    }

    std::fclose(fp);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK)
    {
        removeFileIfExists(filePath);
        return -1;
    }

    return 0;
}

int OTA::parseManifest(const std::string& jsonText, Manifest& manifest)
{
    try
    {
        Manifest m;
        json j = json::parse(jsonText);

        m.version = versionFromJson(j.at("version"));
        m.url = j.at("url").get<std::string>();
        m.sha256 = j.at("sha256").get<std::string>();
        m.size = j.at("size").get<std::size_t>();
        m.force = j.value("force", false);

        if (validateManifest(m) != 0)
        {
            return -1;
        }

        manifest = std::move(m);
        return 0;
    }
    catch (...)
    {
        return -1;
    }
}

int OTA::validateManifest(const Manifest& manifest) const
{
    if (!isValidVersion(manifest.version))
    {
        return -1;
    }

    if (manifest.url.empty())
    {
        return -1;
    }

    if (manifest.size == 0)
    {
        return -1;
    }

    if (config_.strictManifestCheck && !isValidSha256(manifest.sha256))
    {
        return -1;
    }

    return 0;
}

// ========================= verify / file utils =========================

int OTA::calcFileSha256(const std::string& filePath, std::string& sha256)
{
    std::ifstream ifs(filePath, std::ios::binary);
    if (!ifs)
    {
        return -1;
    }

    SHA256_CTX ctx;
    SHA256_Init(&ctx);

    char buf[4096];
    while (ifs.read(buf, sizeof(buf)) || ifs.gcount() > 0)
    {
        SHA256_Update(&ctx, buf, static_cast<std::size_t>(ifs.gcount()));
    }

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_Final(hash, &ctx);

    std::ostringstream oss;
    oss << std::hex << std::nouppercase << std::setfill('0');
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
    {
        oss << std::setw(2) << static_cast<int>(hash[i]);
    }

    sha256 = oss.str();
    return 0;
}

int OTA::getFileSize(const std::string& filePath, std::size_t& size)
{
    std::ifstream ifs(filePath, std::ios::binary | std::ios::ate);
    if (!ifs)
    {
        return -1;
    }

    std::streampos pos = ifs.tellg();
    if (pos < 0)
    {
        return -1;
    }

    size = static_cast<std::size_t>(pos);
    return 0;
}

int OTA::removeFileIfExists(const std::string& filePath)
{
    if (filePath.empty())
    {
        return -1;
    }

    if (::access(filePath.c_str(), F_OK) != 0)
    {
        return 0;
    }

    if (::remove(filePath.c_str()) != 0)
    {
        return -1;
    }

    return 0;
}

int OTA::syncFile(const std::string& filePath)
{
    return syncSingleFile(filePath);
}

int OTA::atomicReplaceFile(const std::string& srcFile, const std::string& dstFile)
{
    return atomicReplaceByRename(srcFile, dstFile);
}

// ========================= version persistence =========================

int OTA::loadCurrentVersion()
{
    if (config_.versionFilePath.empty())
    {
        return 0;
    }

    std::ifstream ifs(config_.versionFilePath);
    if (!ifs)
    {
        return 0;
    }

    try
    {
        json j;
        ifs >> j;
        Version v = versionFromJson(j);

        if (!isValidVersion(v))
        {
            return -1;
        }

        config_.currentVersion = v;
        return 0;
    }
    catch (...)
    {
        return -1;
    }
}

int OTA::saveCurrentVersion(const Version& version)
{
    config_.currentVersion = version;

    if (persistVersionCallback_)
    {
        if (persistVersionCallback_(version) != 0)
        {
            return -1;
        }
    }

    if (config_.versionFilePath.empty())
    {
        return 0;
    }

    const std::string tmpPath = config_.versionFilePath + ".tmp";

    {
        std::ofstream ofs(tmpPath, std::ios::trunc);
        if (!ofs)
        {
            return -1;
        }

        ofs << versionToJson(version).dump(4);
        ofs.flush();

        if (!ofs.good())
        {
            return -1;
        }
    }

    if (syncFile(tmpPath) != 0)
    {
        return -1;
    }

    return atomicReplaceFile(tmpPath, config_.versionFilePath);
}

// ========================= boot meta =========================

int OTA::loadBootMeta()
{
    std::ifstream ifs(config_.bootMetaPath);
    if (!ifs)
    {
        return resetBootMetaToDefault();
    }

    try
    {
        json j;
        ifs >> j;

        bootMeta_.activeSlot =
            (j.value("activeSlot", std::string("A")) == "A") ? Slot::A : Slot::B;
        bootMeta_.pendingSlot =
            (j.value("pendingSlot", std::string("A")) == "A") ? Slot::A : Slot::B;
        bootMeta_.upgradePending = j.value("upgradePending", false);
        bootMeta_.bootSuccess = j.value("bootSuccess", true);
        bootMeta_.retryCount = j.value("retryCount", 0);

        if (j.contains("activeVersion"))
        {
            bootMeta_.activeVersion = versionFromJson(j.at("activeVersion"));
        }
        else
        {
            bootMeta_.activeVersion = config_.currentVersion;
        }

        if (j.contains("pendingVersion"))
        {
            bootMeta_.pendingVersion = versionFromJson(j.at("pendingVersion"));
        }
        else
        {
            bootMeta_.pendingVersion = bootMeta_.activeVersion;
        }

        if (!isValidVersion(bootMeta_.activeVersion) ||
            !isValidVersion(bootMeta_.pendingVersion) ||
            bootMeta_.retryCount < 0)
        {
            return resetBootMetaToDefault();
        }

        return 0;
    }
    catch (...)
    {
        return resetBootMetaToDefault();
    }
}

int OTA::saveBootMeta() const
{
    if (config_.bootMetaPath.empty())
    {
        return -1;
    }

    const std::string tmpPath = config_.bootMetaPath + ".tmp";

    {
        json j;
        j["activeSlot"] = (bootMeta_.activeSlot == Slot::A) ? "A" : "B";
        j["pendingSlot"] = (bootMeta_.pendingSlot == Slot::A) ? "A" : "B";
        j["upgradePending"] = bootMeta_.upgradePending;
        j["bootSuccess"] = bootMeta_.bootSuccess;
        j["retryCount"] = bootMeta_.retryCount;
        j["activeVersion"] = versionToJson(bootMeta_.activeVersion);
        j["pendingVersion"] = versionToJson(bootMeta_.pendingVersion);

        std::ofstream ofs(tmpPath, std::ios::trunc);
        if (!ofs)
        {
            return -1;
        }

        ofs << j.dump(4);
        ofs.flush();

        if (!ofs.good())
        {
            return -1;
        }
    }

    if (syncSingleFile(tmpPath) != 0)
    {
        return -1;
    }

    return atomicReplaceByRename(tmpPath, config_.bootMetaPath);
}

int OTA::resetBootMetaToDefault()
{
    bootMeta_ = BootMeta{};
    bootMeta_.activeSlot = Slot::A;
    bootMeta_.pendingSlot = Slot::A;
    bootMeta_.upgradePending = false;
    bootMeta_.bootSuccess = true;
    bootMeta_.retryCount = 0;
    bootMeta_.activeVersion = config_.currentVersion;
    bootMeta_.pendingVersion = config_.currentVersion;

    return saveBootMeta();
}

// ========================= slot / boot target =========================

int OTA::writeToStandbySlot(const std::string& srcFile, const std::string& dstFile)
{
    const std::string tmpFile = dstFile + ".tmp";

    removeFileIfExists(tmpFile);

    std::ifstream in(srcFile, std::ios::binary);
    if (!in)
    {
        return -1;
    }

    std::ofstream out(tmpFile, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        return -1;
    }

    out << in.rdbuf();
    out.flush();

    if (!out.good())
    {
        return -1;
    }

    out.close();
    in.close();

    struct stat st {};
    if (::stat(srcFile.c_str(), &st) == 0)
    {
        ::chmod(tmpFile.c_str(), st.st_mode);
    }

    if (syncFile(tmpFile) != 0)
    {
        return -1;
    }

    return atomicReplaceFile(tmpFile, dstFile);
}

int OTA::setBootTarget(Slot slot)
{
    if (setBootTargetCallback_)
    {
        return setBootTargetCallback_(slot);
    }

    std::cout << "[OTA] setBootTarget callback not registered, only boot meta is updated\n";
    (void)slot;
    return 0;
}

// ========================= health / reboot =========================

int OTA::healthCheck()
{
    if (healthCheckCallback_)
    {
        int ret = healthCheckCallback_();
        if (ret == 0)
        {
            std::cout << "[OTA] health check success\n";
        }
        else
        {
            std::cerr << "[OTA] health check failed\n";
        }
        return ret;
    }

    std::cout << "[OTA] health check callback not registered, default pass\n";
    return 0;
}

int OTA::rebootSystem()
{
    if (!config_.autoReboot)
    {
        return 0;
    }

    if (rebootCallback_)
    {
        return rebootCallback_();
    }

    ::sync();
    int ret = std::system("reboot");
    return (ret == 0) ? 0 : -1;
}

// ========================= curl callback =========================

std::size_t OTA::writeToString(void* contents, std::size_t size, std::size_t nmemb, void* userp)
{
    if (contents == nullptr || userp == nullptr)
    {
        return 0;
    }

    std::size_t total = size * nmemb;
    std::string* str = static_cast<std::string*>(userp);
    str->append(static_cast<const char*>(contents), total);
    return total;
}

std::size_t OTA::writeToFile(void* contents, std::size_t size, std::size_t nmemb, void* userp)
{
    if (contents == nullptr || userp == nullptr)
    {
        return 0;
    }

    std::size_t written = std::fwrite(contents, size, nmemb, static_cast<FILE*>(userp));
    return written * size;
}


