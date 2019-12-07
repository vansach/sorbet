#ifndef SORBET_WORKERPOOL_H
#define SORBET_WORKERPOOL_H

#include "common/common.h"
#include "spdlog/spdlog.h"
namespace spd = spdlog;
namespace sorbet {
class WorkerPool {
public:
    inline static constexpr std::chrono::milliseconds BLOCK_INTERVAL() {
        using namespace std::chrono_literals;
        return 250ms;
    }
    // [IDE] We want blocking operations to be more responsive to preemption requests.
    inline static constexpr std::chrono::milliseconds PREEMPTION_BLOCK_INTERVAL() {
        using namespace std::chrono_literals;
        return 20ms;
    }
    typedef std::function<void()> Task;
    static std::unique_ptr<WorkerPool> create(int size, spd::logger &logger);
    virtual void multiplexJob(std::string_view taskName, Task t) = 0;
    virtual ~WorkerPool() = 0;
    WorkerPool() = default;
    WorkerPool(WorkerPool &) = delete;
    WorkerPool(const WorkerPool &) = delete;
    WorkerPool &operator=(WorkerPool &&) = delete;
    WorkerPool &operator=(const WorkerPool &) = delete;
};
};     // namespace sorbet
#endif // SORBET_WORKERPOOL_H
