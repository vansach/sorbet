#ifndef RUBY_TYPER_LSP_LSPTYPECHECKERCOORDINATOR_H
#define RUBY_TYPER_LSP_LSPTYPECHECKERCOORDINATOR_H

#include "common/concurrency/WorkerPool.h"
#include "main/lsp/LSPTypechecker.h"

namespace sorbet::realmain::lsp {
/**
 * Handles typechecking and other queries. Can either operate in single-threaded mode (in which lambdas passed to
 * syncRun/asyncRun run-to-completion immediately) or dedicated-thread mode (in which lambdas are enqueued to execute on
 * thread).
 */
class LSPTypecheckerCoordinator final {
    // TODO(jvilk): Switch LSPTypecheckerCoordinator interface to use PreemptionTask instead of std::functions.
    class Task : public core::GlobalState::PreemptionTask {
    private:
        const std::function<void()> lambda;

    public:
        Task(std::function<void()> &&lambda);
        void run() override;
    };

    /** Contains a queue of tasks to run on the typechecking thread. */
    BlockingUnBoundedQueue<std::shared_ptr<Task>> tasks;
    /** If 'true', the coordinator should terminate immediately. */
    bool shouldTerminate;
    /** LSPTypecheckerCoordinator delegates typechecking operations to LSPTypechecker. */
    LSPTypechecker typechecker;
    std::shared_ptr<const LSPConfiguration> config;
    /** If 'true', then the typechecker is running on a dedicated thread. */
    bool hasDedicatedThread;

    WorkerPool &workers;

    /**
     * Runs the provided function on the typechecker thread.
     */
    void asyncRunInternal(std::shared_ptr<Task> lambda);

public:
    LSPTypecheckerCoordinator(const std::shared_ptr<const LSPConfiguration> &config, WorkerPool &workers);

    /**
     * Typechecks the given file asynchronously on the slow path with exclusive access to typechecker. Returns when
     * typechecking has begun.
     */
    void typecheckAsync(std::shared_ptr<LSPFileUpdates> updates);

    /**
     * Like asyncRun, but:
     * - Blocks until `lambda` completes.
     * - If a slow path is currently running, it will preempt the slow path via the passed-in global state.
     * - Does not have access to WorkerPool. Workers cannot be used during preemption. If workers are needed, use
     * syncRun.
     */
    void syncRunPreempt(std::function<void(LSPTypechecker &)> &&lambda, core::GlobalState &initialGS);

    /**
     * syncRun, except the function receives direct access to WorkerPool.
     * Note: This is a separate interface as syncRun as it is needed for preemptible slow path.
     */
    void syncRun(std::function<void(LSPTypechecker &, WorkerPool &)> &&lambda);

    /**
     * Safely shuts down the typechecker and returns the final GlobalState object. Blocks until typechecker completes
     * final operation.
     */
    std::unique_ptr<core::GlobalState> shutdown();

    /** Runs the typechecker in a dedicated thread. */
    std::unique_ptr<Joinable> startTypecheckerThread();
};
} // namespace sorbet::realmain::lsp

#endif
