#ifndef MAIN_LSP_WRAPPER_H
#define MAIN_LSP_WRAPPER_H

#include "spdlog/spdlog.h"
// has to come before the next spdlog include. This comment stops formatter from reordering them
#include "main/lsp/LSPMessage.h"
#include "main/lsp/lsp.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include <string_view>
namespace sorbet::realmain::lsp {

class LSPOutput;
class LSPProgrammaticInput;
class LSPConfiguration;

class LSPWrapper final {
    /** The LSP 'server', which runs in the same thread as LSPWrapper (unless multithreading is enabled) */
    const std::shared_ptr<LSPLoop> lspLoop;
    const std::shared_ptr<LSPConfiguration> config_;
    const std::shared_ptr<LSPOutput> output;

    /** If multithreading is enabled, contains the thread running LSPLoop. */
    std::unique_ptr<Joinable> lspThread;
    /** If multithreading mode is enabled, contains the input object used to feed messages to lspThread. */
    const std::shared_ptr<LSPProgrammaticInput> input;

    /**
     * Sorbet assumes we 'own' the following three objects; keep them alive to avoid memory errors.
     */
    const std::unique_ptr<WorkerPool> workers;
    const std::shared_ptr<spd::sinks::ansicolor_stderr_sink_mt> stderrColorSink;
    const std::shared_ptr<spd::logger> typeErrorsConsole;

    static std::unique_ptr<LSPWrapper>
    createInternal(std::unique_ptr<core::GlobalState> gs, std::shared_ptr<options::Options> options,
                   const std::shared_ptr<spdlog::logger> &logger, bool disableFastPath,
                   std::optional<std::function<void(std::unique_ptr<LSPMessage>)>> &&processMessage,
                   std::shared_ptr<spd::sinks::ansicolor_stderr_sink_mt> stderrColorSink = nullptr,
                   std::shared_ptr<spd::logger> typeErrorsConsole = nullptr);

    static std::unique_ptr<LSPWrapper>
    createInternal(std::string_view rootPath, std::shared_ptr<options::Options> options,
                   std::optional<std::function<void(std::unique_ptr<LSPMessage>)>> &&processMessage,
                   int numWorkerThreads, bool disableFastPath);

public:
    enum class LSPExperimentalFeature {
        Autocomplete = 4,
        WorkspaceSymbols = 5,
        DocumentSymbol = 6,
        SignatureHelp = 7,
        QuickFix = 8,
        DocumentHighlight = 9,
    };

    /**
     * Raw constructor; use create* factory methods to construct the wrapper. If `processMessage` is defined, LSPWrapper
     * will be created in multi-threading mode. Note: Constructor is unwieldy so we can make class fields `const`.
     */
    LSPWrapper(std::shared_ptr<options::Options> opts, std::shared_ptr<LSPLoop> lspLoop,
               std::shared_ptr<LSPConfiguration> config, std::shared_ptr<LSPOutput> output, bool multithreadingEnabled,
               std::unique_ptr<WorkerPool> workers,
               std::shared_ptr<spd::sinks::ansicolor_stderr_sink_mt> stderrColorSink,
               std::shared_ptr<spd::logger> typeErrorsConsole);

    static std::unique_ptr<LSPWrapper> createSingleThreaded(std::unique_ptr<core::GlobalState> gs,
                                                            std::shared_ptr<options::Options> options,
                                                            const std::shared_ptr<spdlog::logger> &logger,
                                                            bool disableFastPath = false);
    static std::unique_ptr<LSPWrapper>
    createSingleThreaded(std::string_view rootPath = std::string_view(),
                         std::shared_ptr<options::Options> options = std::make_shared<options::Options>(),
                         bool disableFastPath = false);
    static std::unique_ptr<LSPWrapper>
    createMultiThreaded(std::function<void(std::unique_ptr<LSPMessage>)> &&processMessage,
                        std::string_view rootPath = std::string_view(),
                        std::shared_ptr<options::Options> options = std::make_shared<options::Options>(),
                        int numWorkerThreads = 1, bool disableFastPath = false);

    // N.B.: Sorbet assumes we 'own' this object; keep it alive to avoid memory errors.
    const std::shared_ptr<options::Options> opts;

    /** If true, then the LSP wrapper is running LSPLoop on a dedicated thread. */
    const bool multithreadingEnabled;

    /**
     * Define so:
     * 1. We can properly destruct unique_ptr<LSPOutputToVector> (which the default destructor can't delete since we
     * forward decl it in the header)
     * 2. We can stop lspThread if running in multithreaded mode.
     */
    ~LSPWrapper();

    const LSPConfiguration &config() const;

    /**
     * [Single-threaded only] Send a message to LSP, and returns any responses.
     */
    std::vector<std::unique_ptr<LSPMessage>> getLSPResponsesFor(std::unique_ptr<LSPMessage> message);

    /**
     * [Single-threaded only] Send a message to LSP, and returns any responses.
     */
    std::vector<std::unique_ptr<LSPMessage>> getLSPResponsesFor(const std::string &json);

    /**
     * [Single-threaded only] Sends multiple messages to LSP, and returns any responses.
     */
    std::vector<std::unique_ptr<LSPMessage>> getLSPResponsesFor(std::vector<std::unique_ptr<LSPMessage>> &messages);

    /**
     * [Multi-threaded only] Sends one message to LSP.
     */
    void send(std::unique_ptr<LSPMessage> message);

    /**
     * [Multi-threaded only] Sends multiple messages to LSP.
     */
    void send(std::vector<std::unique_ptr<LSPMessage>> &messages);

    /**
     * Enable an experimental LSP feature.
     * Note: Use this method *before* the client performs initialization with the server.
     */
    void enableExperimentalFeature(LSPExperimentalFeature feature);

    /**
     * Enable all experimental LSP features.
     * Note: Use this method *before* the client performs initialization with the server.
     */
    void enableAllExperimentalFeatures();

    /**
     * (For tests only) Retrieve the number of times typechecking has run.
     */
    int getTypecheckCount();
};

} // namespace sorbet::realmain::lsp

#endif // MAIN_LSP_WRAPPER_H
