//
// utils.hpp
//
#ifndef __UTILS_HPP__
#define __UTILS_HPP__

#include <chrono>
#include <string>
#include <ostream>

//
// Helper macros to unify logging of information and error messages
//
#define INFO_ENABLED(enabled) inline bool g_InfoEnabled = enabled;

// Strips the '/path/' prefix from the file name
constexpr const char* __fname__(const char* file, int i)
{
    return (i == 0) ? (file) : (*(file + i) == '/' ? (file + i + 1) : __fname__(file, i - 1));
}

#define __FNAME__  __fname__(__FILE__, sizeof(__FILE__)-1)

#define MSG(type, msg) do { \
    std::cout << "[" << getpid() << "] " << type \
              << " [" << __FNAME__ << ":" << __LINE__ << "] " \
              << __func__ << ": " << msg << std::endl; \
} while(0)

#define INFO(msg)  if(g_InfoEnabled)MSG("INFO", msg)
#define ERROR(msg) MSG("ERROR", msg)
#define OUT(msg) std::cout << msg << std::endl;
#define FTRACE(msg) std::cout << "### "; MSG("\b", msg)

//
// Helper class to report elapsed time.
// Usage example: StopWatch elapsed("Command took: ");
// Output:        Command took: 1.030318 sec
//
class StopWatch
{
    std::string prefix;
    std::chrono::time_point<std::chrono::high_resolution_clock> start;

public:
    StopWatch(const char* _prefix="") : 
        prefix(_prefix), start(std::chrono::high_resolution_clock::now()) {}

    ~StopWatch()
    {
        auto stop = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> duration = stop - start;
        std::cout << prefix << duration.count() << " sec" << std::endl;

        // // Convert to a fixed-point duration (e.g., milliseconds)
        // auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration);
        // std::cout << prefix << duration_ms.count() << " ms" << std::endl;
    }   
};

// Converts a number of bytes into a human-readable format (e.g., '128 GB', '64 MB').
inline std::string BytesToStr(std::size_t size)
{
    if(size == 0)
        return "0 Bytes";

    struct Unit
    {
        std::size_t threshold{0};
        const char* label{nullptr};
    };

    static constexpr Unit units[] =
    {
        { 1024UL * 1024 * 1024 * 1024, "TB" },
        { 1024UL * 1024 * 1024,        "GB" },
        { 1024UL * 1024,               "MB" },
        { 1024UL,                      "KB" },
        { 1,                           "Bytes" }
    };

    std::string result;

    for(const auto& [threshold, label] : units)
    {
        if(size >= threshold)
        {
            if(!result.empty())
                result += " ";
            result += std::to_string(size / threshold) + " " + label;
            size %= threshold;
        }
    }

    return result;
}

#include <sys/wait.h>
#include <vector>
#include <functional>
#include <cstring>   // For std::strerror

inline bool ForkAndWait(int nProcs, std::function<void(int)> childProc)
{
    std::vector<pid_t> childPids;
    childPids.reserve(nProcs);

    for(int i = 0; i < nProcs; ++i)
    {
        pid_t pid = fork();

        if(pid < 0)
        {
            // If fork failed, trigger a mass shutdown
            std::cerr << "fork failed: " << std::strerror(errno) << std::endl;
            for(pid_t p : childPids)
                kill(p, SIGTERM);

            for(pid_t p : childPids)
                waitpid(p, nullptr, 0);
                
            return false;
        }
        else if(pid == 0)
        {
            childProc(i);
            _exit(0);
        }

        childPids.push_back(pid);
    }

    bool allSuccessful = true;

    // Monitor all children; react immediately if any finish out of order
    while(!childPids.empty())
    {
        int exitStatus;

        // -1 tells waitpid to report on ANY child that changes state
        pid_t finishedPid = waitpid(-1, &exitStatus, 0);

        if(finishedPid > 0)
        {
            auto it = std::find(childPids.begin(), childPids.end(), finishedPid);
            if(it != childPids.end())
                childPids.erase(it);

            // If a child crashed or returned non-zero, trigger a mass shutdown
            if(!WIFEXITED(exitStatus) || WEXITSTATUS(exitStatus) != 0)
            {
                allSuccessful = false;

                // Stop all other running siblings
                for(pid_t remainingPid : childPids)
                    kill(remainingPid, SIGTERM);

                // Final cleanup: Ensure no zombies remain in the process table
                while(!childPids.empty())
                {
                    pid_t reaped = waitpid(-1, nullptr, 0);
                    auto itRem = std::find(childPids.begin(), childPids.end(), reaped);
                    if(itRem != childPids.end())
                        childPids.erase(itRem);
                }
                break;
            }
        }
    }

    return allSuccessful;
}

#endif //__UTILS_HPP__
