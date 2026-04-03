#include "/home/xu/GetWay/Deamon/Deamon.h"



DaemonManager* DaemonManager::instance_ = nullptr;
volatile std::sig_atomic_t DaemonManager::signalStopFlag_ = 0;

// ==================== 生命周期 ====================

DaemonManager::~DaemonManager()
{
    bool hasLiveChild = false;
    for (const auto& proc : processes_)
    {
        if (proc.pid > 0)
        {
            hasLiveChild = true;
            break;
        }
    }

    if (running_ || hasLiveChild)
    {
        stop();
        stopAll();
    }

    running_ = false;
    instance_ = nullptr;
}

// ==================== 配置接口 ====================

void DaemonManager::setDaemonMode(bool enable) noexcept
{
    daemonMode_ = enable;
}

void DaemonManager::setRedirectStdIO(bool enable) noexcept
{
    redirectStdIO_ = enable;
}

void DaemonManager::setCheckIntervalMs(int ms) noexcept
{
    checkIntervalMs_ = (ms > 0) ? ms : 200;
}

void DaemonManager::setRestartDelaySec(int sec) noexcept
{
    restartDelaySec_ = (sec >= 0) ? sec : 1;
}

void DaemonManager::setStopTimeoutSec(int sec) noexcept
{
    stopTimeoutSec_ = (sec > 0) ? sec : 5;
}

void DaemonManager::setMaxCrashCount(int count) noexcept
{
    maxCrashCount_ = (count > 0) ? count : 20;
}

void DaemonManager::setStdoutFile(const std::string& file)
{
    stdoutFile_ = file;
}

void DaemonManager::setStderrFile(const std::string& file)
{
    stderrFile_ = file;
}

// ==================== 进程管理 ====================

int DaemonManager::addProcess(const std::string& name,
                              const std::string& program,
                              const std::vector<std::string>& args,
                              bool autoRestart,
                              int maxRestartCount)
{
    if (runCalled_)
    {
        std::cerr << "DaemonManager::addProcess failed: run() already called.\n";
        return -1;
    }

    if (name.empty() || program.empty())
    {
        std::cerr << "DaemonManager::addProcess failed: name/program is empty.\n";
        return -1;
    }

    if (findProcessByName(name) != nullptr)
    {
        std::cerr << "DaemonManager::addProcess failed: duplicate process name: " << name << "\n";
        return -1;
    }

    Process proc(name, program, args, autoRestart, (maxRestartCount > 0 ? maxRestartCount : 10));
    processes_.push_back(proc);
    return 0;
}

std::size_t DaemonManager::processCount() const noexcept
{
    return processes_.size();
}

bool DaemonManager::empty() const noexcept
{
    return processes_.empty();
}

bool DaemonManager::isRunning() const noexcept
{
    return running_;
}

bool DaemonManager::isStopping() const noexcept
{
    return stopping_;
}

// ==================== 主流程 ====================

int DaemonManager::run()
{
    if (runCalled_)
    {
        std::cerr << "DaemonManager::run failed: run() can only be called once.\n";
        return -1;
    }

    if (processes_.empty())
    {
        std::cerr << "DaemonManager::run failed: no managed process configured.\n";
        return -1;
    }

    runCalled_ = true;
    resetRuntimeState();

    instance_ = this;

    if (daemonMode_)
    {
        if (daemonize() != 0)
        {
            std::cerr << "DaemonManager::run failed: daemonize failed.\n";
            return -1;
        }
    }

    instance_ = this;

    if (redirectStdIO_)
    {
        if (redirectStdIO() != 0)
        {
            std::cerr << "DaemonManager::run failed: redirectStdIO failed.\n";
            return -1;
        }
    }

    if (installSignalHandlers() != 0)
    {
        std::cerr << "DaemonManager::run failed: installSignalHandlers failed.\n";
        return -1;
    }

    if (startAll() != 0)
    {
        std::cerr << "DaemonManager::run failed: startAll failed.\n";
        stopAll();
        return -1;
    }

    running_ = true;

    int ret = monitorLoop();

    stopAll();
    running_ = false;
    return ret;
}

void DaemonManager::stop() noexcept
{
    stopping_ = true;
    signalStopFlag_ = 1;
}

int DaemonManager::startAll()
{
    if (processes_.empty())
    {
        std::cerr << "DaemonManager::startAll failed: no managed process.\n";
        return -1;
    }

    for (auto& proc : processes_)
    {
        if (startProcess(proc) != 0)
        {
            std::cerr << "DaemonManager::startAll failed: start process [" << proc.name << "] failed.\n";
            return -1;
        }
    }

    return 0;
}

int DaemonManager::stopAll()
{
    stopping_ = true;
    handleStopSignalIfNeeded();

    for (auto& proc : processes_)
    {
        if (proc.pid > 0)
        {
            (void)stopProcess(proc, SIGTERM);
        }
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(stopTimeoutSec_);

    while (std::chrono::steady_clock::now() < deadline)
    {
        while (true)
        {
            int status = 0;
            pid_t pid = ::waitpid(-1, &status, WNOHANG);
            if (pid > 0)
            {
                (void)handleChildExit(pid, status);
                continue;
            }

            if (pid == 0)
            {
                break;
            }

            if (pid < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                break;
            }
        }

        bool anyAlive = false;
        for (auto& proc : processes_)
        {
            if (proc.pid > 0)
            {
                if (isProcessRunning(proc.pid))
                {
                    anyAlive = true;
                }
                else
                {
                    markProcessStopped(proc);
                }
            }
        }

        if (!anyAlive)
        {
            running_ = false;
            return 0;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    for (auto& proc : processes_)
    {
        if (proc.pid > 0)
        {
            (void)forceKillProcess(proc);
        }
    }

    auto killDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);

    while (std::chrono::steady_clock::now() < killDeadline)
    {
        while (true)
        {
            int status = 0;
            pid_t pid = ::waitpid(-1, &status, WNOHANG);
            if (pid > 0)
            {
                (void)handleChildExit(pid, status);
                continue;
            }

            if (pid == 0)
            {
                break;
            }

            if (pid < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                break;
            }
        }

        bool anyAlive = false;
        for (auto& proc : processes_)
        {
            if (proc.pid > 0)
            {
                if (isProcessRunning(proc.pid))
                {
                    anyAlive = true;
                }
                else
                {
                    markProcessStopped(proc);
                }
            }
        }

        if (!anyAlive)
        {
            running_ = false;
            return 0;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    running_ = false;
    return -1;
}

// ==================== 单个子进程操作 ====================

int DaemonManager::startProcess(Process& proc)
{
    if (stopping_)
    {
        std::cerr << "DaemonManager::startProcess skipped: manager is stopping.\n";
        return -1;
    }

    if (proc.name.empty() || proc.program.empty())
    {
        std::cerr << "DaemonManager::startProcess failed: invalid process config.\n";
        return -1;
    }

    if (proc.pid > 0 && isProcessRunning(proc.pid))
    {
        return 0;
    }

    if (::access(proc.program.c_str(), X_OK) != 0)
    {
        std::cerr << "DaemonManager::startProcess failed: program not executable: "
                  << proc.program << ", errno=" << errno
                  << ", msg=" << std::strerror(errno) << "\n";
        return -1;
    }

    pid_t pid = ::fork();
    if (pid < 0)
    {
        std::cerr << "DaemonManager::startProcess failed: fork failed, errno="
                  << errno << ", msg=" << std::strerror(errno) << "\n";
        return -1;
    }

    if (pid == 0)
    {
        ::signal(SIGTERM, SIG_DFL);
        ::signal(SIGINT,  SIG_DFL);
        ::signal(SIGQUIT, SIG_DFL);
        ::signal(SIGHUP,  SIG_DFL);
        ::signal(SIGCHLD, SIG_DFL);
        ::signal(SIGPIPE, SIG_DFL);

        ::setpgid(0, 0);

        std::vector<char*> argv = buildArgv(proc);
        ::execv(proc.program.c_str(), argv.data());

        _exit(127);
    }

    if (::setpgid(pid, pid) != 0)
    {
        if (errno != EACCES && errno != ESRCH)
        {
            std::cerr << "DaemonManager::startProcess warning: setpgid failed for ["
                      << proc.name << "], errno=" << errno
                      << ", msg=" << std::strerror(errno) << "\n";
        }
    }

    proc.pid = pid;
    return 0;
}

int DaemonManager::stopProcess(Process& proc, int sig)
{
    if (proc.pid <= 0)
    {
        return 0;
    }

    pid_t pid = proc.pid;

    if (::kill(-pid, sig) != 0)
    {
        if (errno != ESRCH)
        {
            if (::kill(pid, sig) != 0)
            {
                if (errno == ESRCH)
                {
                    markProcessStopped(proc);
                    return 0;
                }

                std::cerr << "DaemonManager::stopProcess failed: process [" << proc.name
                          << "], pid=" << pid
                          << ", sig=" << sig
                          << ", errno=" << errno
                          << ", msg=" << std::strerror(errno) << "\n";
                return -1;
            }
        }
        else
        {
            markProcessStopped(proc);
        }
    }

    return 0;
}

int DaemonManager::forceKillProcess(Process& proc)
{
    return stopProcess(proc, SIGKILL);
}

int DaemonManager::restartProcess(Process& proc)
{
    if (stopping_ || signalStopFlag_)
    {
        return -1;
    }

    if (restartDelaySec_ > 0)
    {
        for (int i = 0; i < restartDelaySec_ * 10; ++i)
        {
            if (stopping_ || signalStopFlag_)
            {
                return -1;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    return startProcess(proc);
}

// ==================== 守护进程内部逻辑 ====================

int DaemonManager::daemonize()
{
    pid_t pid = ::fork();
    if (pid < 0)
    {
        return -1;
    }

    if (pid > 0)
    {
        _exit(EXIT_SUCCESS);
    }

    if (::setsid() < 0)
    {
        return -1;
    }

    struct sigaction sa {};
    sa.sa_handler = SIG_IGN;
    ::sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (::sigaction(SIGHUP, &sa, nullptr) != 0)
    {
        return -1;
    }

    pid = ::fork();
    if (pid < 0)
    {
        return -1;
    }

    if (pid > 0)
    {
        _exit(EXIT_SUCCESS);
    }

    ::umask(0);

    if (::chdir("/") != 0)
    {
        return -1;
    }

    return 0;
}

int DaemonManager::redirectStdIO()
{
    int fdIn = ::open("/dev/null", O_RDONLY);
    if (fdIn < 0)
    {
        return -1;
    }

    std::string outPath = stdoutFile_.empty() ? "/dev/null" : stdoutFile_;
    std::string errPath = stderrFile_.empty() ? "/dev/null" : stderrFile_;

    int fdOut = ::open(outPath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fdOut < 0)
    {
        ::close(fdIn);
        return -1;
    }

    int fdErr = ::open(errPath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fdErr < 0)
    {
        ::close(fdIn);
        ::close(fdOut);
        return -1;
    }

    if (::dup2(fdIn, STDIN_FILENO) < 0 ||
        ::dup2(fdOut, STDOUT_FILENO) < 0 ||
        ::dup2(fdErr, STDERR_FILENO) < 0)
    {
        ::close(fdIn);
        ::close(fdOut);
        ::close(fdErr);
        return -1;
    }

    if (fdIn > STDERR_FILENO)  { ::close(fdIn); }
    if (fdOut > STDERR_FILENO) { ::close(fdOut); }
    if (fdErr > STDERR_FILENO) { ::close(fdErr); }

    return 0;
}

int DaemonManager::installSignalHandlers()
{
    struct sigaction sa {};
    sa.sa_handler = &DaemonManager::signalHandler;
    ::sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if (::sigaction(SIGTERM, &sa, nullptr) != 0) { return -1; }
    if (::sigaction(SIGINT,  &sa, nullptr) != 0) { return -1; }
    if (::sigaction(SIGQUIT, &sa, nullptr) != 0) { return -1; }
    if (::sigaction(SIGHUP,  &sa, nullptr) != 0) { return -1; }

    struct sigaction ign {};
    ign.sa_handler = SIG_IGN;
    ::sigemptyset(&ign.sa_mask);
    ign.sa_flags = 0;

    if (::sigaction(SIGPIPE, &ign, nullptr) != 0) { return -1; }

    return 0;
}

int DaemonManager::monitorLoop()
{
    while (running_ || !stopping_)
    {
        handleStopSignalIfNeeded();

        if (stopping_)
        {
            break;
        }

        bool reapedChild = false;

        while (true)
        {
            int status = 0;
            pid_t pid = ::waitpid(-1, &status, WNOHANG);

            if (pid > 0)
            {
                reapedChild = true;
                (void)handleChildExit(pid, status);
                handleStopSignalIfNeeded();

                if (stopping_)
                {
                    break;
                }
                continue;
            }

            if (pid == 0)
            {
                break;
            }

            if (pid < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                break;
            }
        }

        if (stopping_)
        {
            break;
        }

        bool anyAlive = false;
        for (auto& proc : processes_)
        {
            if (proc.pid > 0)
            {
                if (isProcessRunning(proc.pid))
                {
                    anyAlive = true;
                }
                else
                {
                    markProcessStopped(proc);
                }
            }
        }

        if (!anyAlive)
        {
            break;
        }

        if (!reapedChild)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(checkIntervalMs_));
        }
    }

    return 0;
}

int DaemonManager::handleChildExit(pid_t pid, int status)
{
    Process* proc = findProcessByPid(pid);
    if (proc == nullptr)
    {
        return 0;
    }

    markProcessStopped(*proc);

    if (stopping_ || signalStopFlag_)
    {
        return 0;
    }

    if (isNormalExitStatus(status))
    {
        return 0;
    }

    if (isCrashExitStatus(status))
    {
        ++proc->crashCount;
        ++totalCrashCount_;
    }

    if (!shouldRestart(*proc, status))
    {
        return 0;
    }

    ++proc->restartCount;
    return restartProcess(*proc);
}

void DaemonManager::handleStopSignalIfNeeded() noexcept
{
    if (signalStopFlag_ != 0)
    {
        stopping_ = true;
    }
}

// ==================== 查找/工具 ====================

DaemonManager::Process* DaemonManager::findProcessByPid(pid_t pid) noexcept
{
    for (auto& proc : processes_)
    {
        if (proc.pid == pid)
        {
            return &proc;
        }
    }
    return nullptr;
}

DaemonManager::Process* DaemonManager::findProcessByName(const std::string& name) noexcept
{
    for (auto& proc : processes_)
    {
        if (proc.name == name)
        {
            return &proc;
        }
    }
    return nullptr;
}

const DaemonManager::Process* DaemonManager::findProcessByName(const std::string& name) const noexcept
{
    for (const auto& proc : processes_)
    {
        if (proc.name == name)
        {
            return &proc;
        }
    }
    return nullptr;
}

bool DaemonManager::isProcessRunning(pid_t pid) const noexcept
{
    if (pid <= 0)
    {
        return false;
    }

    if (::kill(pid, 0) == 0)
    {
        return true;
    }

    return (errno == EPERM);
}

bool DaemonManager::shouldRestart(const Process& proc, int status) const noexcept
{
    if (stopping_ || signalStopFlag_)
    {
        return false;
    }

    if (!proc.autoRestart)
    {
        return false;
    }

    if (!isCrashExitStatus(status))
    {
        return false;
    }

    if (proc.restartCount >= proc.maxRestartCount)
    {
        return false;
    }

    if (totalCrashCount_ >= maxCrashCount_)
    {
        return false;
    }

    return true;
}

bool DaemonManager::isNormalExitStatus(int status) const noexcept
{
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

bool DaemonManager::isCrashExitStatus(int status) const noexcept
{
    if (WIFSIGNALED(status))
    {
        return true;
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
    {
        return true;
    }

    return false;
}

std::vector<char*> DaemonManager::buildArgv(const Process& proc) const
{
    std::vector<char*> argv;
    argv.reserve(proc.args.size() + 2);

    argv.push_back(const_cast<char*>(proc.program.c_str()));
    for (const auto& arg : proc.args)
    {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    return argv;
}

void DaemonManager::resetRuntimeState() noexcept
{
    running_ = false;
    stopping_ = false;
    totalCrashCount_ = 0;
    signalStopFlag_ = 0;

    for (auto& proc : processes_)
    {
        proc.pid = -1;
        proc.restartCount = 0;
        proc.crashCount = 0;
    }
}

void DaemonManager::markProcessStopped(Process& proc) noexcept
{
    proc.pid = -1;
}

// ==================== 信号处理 ====================

void DaemonManager::signalHandler(int sig)
{
    switch (sig)
    {
        case SIGTERM:
        case SIGINT:
        case SIGQUIT:
        case SIGHUP:
            signalStopFlag_ = 1;
            break;
        default:
            break;
    }
}