#pragma once
#include "/home/xu/GetWay/Public/_public.h"

class DaemonManager
{
    public:
        struct Process
        {
            std::string name;                      // 进程名字：gateway / ota
            std::string program;                   // 可执行文件绝对路径
            std::vector<std::string> args;         // 参数列表（不含 argv[0]）

            pid_t pid{-1};                         // 当前运行中的子进程 pid
            int restartCount{0};                   // 当前累计重启次数
            int crashCount{0};                     // 当前累计异常退出次数
            int maxRestartCount{10};               // 单进程最大重启次数
            bool autoRestart{true};                // 异常退出后是否自动重启

            Process() = default;
            Process(const std::string& processName,const std::string& programPath,const std::vector<std::string>& processArgs = {},bool restart = true,int maxRestart = 10): name(processName),program(programPath),args(processArgs),pid(-1),restartCount(0),crashCount(0),maxRestartCount(maxRestart),autoRestart(restart){}
        };

    private:
        std::vector<Process> processes_;           // 所有被守护的子进程

        std::atomic<bool> running_{false};         // 守护管理器是否正在运行
        std::atomic<bool> stopping_{false};        // 普通上下文下的停止请求
        bool runCalled_{false};                    // run() 是否已经调用过

        int totalCrashCount_{0};                   // 全局累计异常退出次数
        int maxCrashCount_{20};                    // 全局崩溃阈值
        int checkIntervalMs_{200};                 // waitpid 轮询间隔
        int restartDelaySec_{1};                   // 子进程退出后重启前等待时间
        int stopTimeoutSec_{5};                    // stopAll 时等待优雅退出超时

        bool daemonMode_{true};                    // 是否转成守护进程
        bool redirectStdIO_{true};                 // 是否重定向 0/1/2
        std::string stdoutFile_;                   // 可选：stdout 重定向文件
        std::string stderrFile_;                   // 可选：stderr 重定向文件

        static DaemonManager* instance_;           // 给静态 signalHandler 使用
        static volatile std::sig_atomic_t signalStopFlag_; // 信号上下文只置位这个标志

    private:
        DaemonManager() = default;
        

        DaemonManager(const DaemonManager&) = delete;
        DaemonManager& operator=(const DaemonManager&) = delete;
    public:
        ~DaemonManager();
    public:
        //  配置接口 
        void setDaemonMode(bool enable) noexcept;
        void setRedirectStdIO(bool enable) noexcept;
        void setCheckIntervalMs(int ms) noexcept;      // ms <= 0 时内部应回退默认值
        void setRestartDelaySec(int sec) noexcept;     // sec < 0 时内部应回退默认值
        void setStopTimeoutSec(int sec) noexcept;      // sec <= 0 时内部应回退默认值
        void setMaxCrashCount(int count) noexcept;     // count <= 0 时内部应回退默认值
        void setStdoutFile(const std::string& file);
        void setStderrFile(const std::string& file);

        //  进程管理 
        int addProcess(const std::string& name,
                    const std::string& program,
                    const std::vector<std::string>& args = {},
                    bool autoRestart = true,
                    int maxRestartCount = 10);

        std::size_t processCount() const noexcept;
        bool empty() const noexcept;

        //  核心函数 
        int run();                     // 守护化 + 启动全部子进程 + 监控
        void stop() noexcept;          // 请求停止（线程/普通上下文可调用）
        int startAll();                // 启动所有子进程
        int stopAll();                 // 先 TERM，再超时后 KILL

        bool isRunning() const noexcept;
        bool isStopping() const noexcept;

    private:
        //  单个子进程操作 
        int startProcess(Process& proc);
        int stopProcess(Process& proc, int sig = SIGTERM);
        int forceKillProcess(Process& proc);
        int restartProcess(Process& proc);

        //  守护进程内部逻辑 
        int daemonize();                           // 后台化
        int redirectStdIO();                      // 重定向标准输入输出错误
        int installSignalHandlers();              // 注册 SIGTERM/SIGINT/SIGCHLD 等
        int monitorLoop();                        // waitpid 监控循环
        int handleChildExit(pid_t pid, int status);
        void handleStopSignalIfNeeded() noexcept; // 把 signalStopFlag_ 同步到 stopping_

        //  查找/工具 
        Process* findProcessByPid(pid_t pid) noexcept;
        Process* findProcessByName(const std::string& name) noexcept;
        const Process* findProcessByName(const std::string& name) const noexcept;

        bool isProcessRunning(pid_t pid) const noexcept;
        bool shouldRestart(const Process& proc, int status) const noexcept;
        bool isNormalExitStatus(int status) const noexcept;
        bool isCrashExitStatus(int status) const noexcept;

        std::vector<char*> buildArgv(const Process& proc) const;
        void resetRuntimeState() noexcept;
        void markProcessStopped(Process& proc) noexcept;

    private:
        //  信号处理 
        static void signalHandler(int sig);
};