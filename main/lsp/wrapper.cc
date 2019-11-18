#include "main/lsp/wrapper.h"
#include "core/errors/namer.h"
#include "main/lsp/LSPInput.h"
#include "main/lsp/LSPOutput.h"
#include "main/lsp/json_types.h"
#include "main/pipeline/pipeline.h"
#include "payload/payload.h"
#include <regex>

using namespace std;

namespace sorbet::realmain::lsp {
namespace {
void fixGS(core::GlobalState &gs, options::Options &opts) {
    if (!opts.stripeMode) {
        // Definitions in multiple locations interact poorly with autoloader this error is enforced
        // in Stripe code.
        gs.suppressErrorClass(sorbet::core::errors::Namer::MultipleBehaviorDefs.code);
    }

    // If we don't tell the errorQueue to ignore flushes, then we won't get diagnostic messages.
    gs.errorQueue->ignoreFlushes = true;
}
} // namespace

vector<unique_ptr<LSPMessage>> LSPWrapper::getLSPResponsesFor(unique_ptr<LSPMessage> message) {
    if (multithreadingEnabled) {
        Exception::raise("getLSPResponsesFor is only possible in single-threaded mode. Use send instead.");
    }
    lspLoop->processRequest(move(message));
    return dynamic_pointer_cast<LSPOutputToVector>(output)->getOutput();
}

vector<unique_ptr<LSPMessage>> LSPWrapper::getLSPResponsesFor(vector<unique_ptr<LSPMessage>> &messages) {
    if (multithreadingEnabled) {
        Exception::raise("getLSPResponsesFor is only possible in single-threaded mode. Use send instead.");
    }
    lspLoop->processRequests(move(messages));
    return dynamic_pointer_cast<LSPOutputToVector>(output)->getOutput();
}

vector<unique_ptr<LSPMessage>> LSPWrapper::getLSPResponsesFor(const string &json) {
    if (multithreadingEnabled) {
        Exception::raise("getLSPResponsesFor is only possible in single-threaded mode. Use send instead.");
    }
    return getLSPResponsesFor(LSPMessage::fromClient(json));
}

void LSPWrapper::send(std::unique_ptr<LSPMessage> message) {
    if (!multithreadingEnabled) {
        Exception::raise("send is only possible in multi-threaded mode. Use getLSPResponsesFor instead.");
    }
    input->write(move(message));
}

void LSPWrapper::send(std::vector<std::unique_ptr<LSPMessage>> &messages) {
    if (!multithreadingEnabled) {
        Exception::raise("send is only possible in multi-threaded mode. Use getLSPResponsesFor instead.");
    }
    input->write(move(messages));
}

LSPWrapper::LSPWrapper(shared_ptr<options::Options> opts, shared_ptr<LSPLoop> lspLoop,
                       shared_ptr<LSPConfiguration> config, shared_ptr<LSPOutput> output, bool multithreadingEnabled,
                       unique_ptr<WorkerPool> workers, shared_ptr<spd::sinks::ansicolor_stderr_sink_mt> stderrColorSink,
                       shared_ptr<spd::logger> typeErrorsConsole)
    : lspLoop(move(lspLoop)), config(move(config)), output(move(output)), input(make_shared<LSPProgrammaticInput>()),
      workers(move(workers)), stderrColorSink(move(stderrColorSink)), typeErrorsConsole(move(typeErrorsConsole)),
      opts(move(opts)), multithreadingEnabled(multithreadingEnabled) {
    if (multithreadingEnabled) {
        if (emscripten_build) {
            Exception::raise("LSPWrapper cannot be used in multithreaded mode in Emscripten.");
        }
        // Start LSPLoop on a dedicated thread.
        lspThread = runInAThread("LSPLoop",
                                 [input = this->input, lspLoop = this->lspLoop]() -> void { lspLoop->runLSP(input); });
    }
}

// All factory constructors lead here. Separate from the actual constructor since const fields need to be initialized in
// initializer list.
unique_ptr<LSPWrapper> LSPWrapper::createInternal(unique_ptr<core::GlobalState> gs,
                                                  shared_ptr<options::Options> options,
                                                  const shared_ptr<spdlog::logger> &logger, bool disableFastPath,
                                                  optional<function<void(unique_ptr<LSPMessage>)>> &&processMessage,
                                                  shared_ptr<spd::sinks::ansicolor_stderr_sink_mt> stderrColorSink,
                                                  shared_ptr<spd::logger> typeErrorsConsole) {
    fixGS(*gs, *options);
    auto workers = WorkerPool::create(options->threads, *logger);
    bool multithreadingEnabled = processMessage.has_value();
    shared_ptr<LSPOutput> output;
    if (multithreadingEnabled) {
        output = make_shared<LSPLambdaOutput>(move(processMessage.value()));
    } else {
        output = make_shared<LSPOutputToVector>();
    }
    // Configure LSPLoop to disable configatron.
    auto config = make_shared<LSPConfiguration>(*options, output, *workers, logger, true, disableFastPath);
    auto lspLoop = make_shared<LSPLoop>(std::move(gs), config);
    return make_unique<LSPWrapper>(move(options), move(lspLoop), move(config), move(output), multithreadingEnabled,
                                   move(workers), move(stderrColorSink), move(typeErrorsConsole));
}

unique_ptr<LSPWrapper> LSPWrapper::createInternal(string_view rootPath, shared_ptr<options::Options> options,
                                                  optional<function<void(unique_ptr<LSPMessage>)>> &&processMessage,
                                                  bool disableFastPath) {
    options->rawInputDirNames.emplace_back(rootPath);

    // All of this stuff is ignored by LSP, but we need it to construct ErrorQueue/GlobalState.
    auto stderrColorSink = make_shared<spd::sinks::ansicolor_stderr_sink_mt>();
    auto logger = make_shared<spd::logger>("console", stderrColorSink);
    auto typeErrorsConsole = make_shared<spd::logger>("typeDiagnostics", stderrColorSink);
    typeErrorsConsole->set_pattern("%v");
    auto gs = make_unique<core::GlobalState>((make_shared<core::ErrorQueue>(*typeErrorsConsole, *logger)));
    unique_ptr<KeyValueStore> kvstore;
    payload::createInitialGlobalState(gs, *options, kvstore);

    return createInternal(move(gs), move(options), logger, disableFastPath, move(processMessage), move(stderrColorSink),
                          move(typeErrorsConsole));
}

unique_ptr<LSPWrapper> LSPWrapper::createSingleThreaded(unique_ptr<core::GlobalState> gs,
                                                        shared_ptr<options::Options> options,
                                                        const shared_ptr<spdlog::logger> &logger,
                                                        bool disableFastPath) {
    return createInternal(move(gs), move(options), logger, disableFastPath, nullopt);
}

unique_ptr<LSPWrapper> LSPWrapper::createSingleThreaded(string_view rootPath, shared_ptr<options::Options> options,
                                                        bool disableFastPath) {
    return createInternal(rootPath, move(options), nullopt, disableFastPath);
}

unique_ptr<LSPWrapper> LSPWrapper::createMultiThreaded(function<void(unique_ptr<LSPMessage>)> &&processMessage,
                                                       string_view rootPath, shared_ptr<options::Options> options,
                                                       bool disableFastPath) {
    return createInternal(rootPath, move(options), move(processMessage), disableFastPath);
}

LSPWrapper::~LSPWrapper() {
    if (multithreadingEnabled) {
        // End input stream so lsploop shuts down.
        input->close();
        // The destructor will wait until the lspThread Joinable destructs, which waits for thread death, which should
        // occur post-close.
    }
}

void LSPWrapper::enableAllExperimentalFeatures() {
    enableExperimentalFeature(LSPExperimentalFeature::Autocomplete);
    enableExperimentalFeature(LSPExperimentalFeature::WorkspaceSymbols);
    enableExperimentalFeature(LSPExperimentalFeature::DocumentHighlight);
    enableExperimentalFeature(LSPExperimentalFeature::DocumentSymbol);
    enableExperimentalFeature(LSPExperimentalFeature::SignatureHelp);
    enableExperimentalFeature(LSPExperimentalFeature::QuickFix);
}

void LSPWrapper::enableExperimentalFeature(LSPExperimentalFeature feature) {
    switch (feature) {
        case LSPExperimentalFeature::Autocomplete:
            opts->lspAutocompleteEnabled = true;
            break;
        case LSPExperimentalFeature::QuickFix:
            opts->lspQuickFixEnabled = true;
            break;
        case LSPExperimentalFeature::WorkspaceSymbols:
            opts->lspWorkspaceSymbolsEnabled = true;
            break;
        case LSPExperimentalFeature::DocumentHighlight:
            opts->lspDocumentHighlightEnabled = true;
            break;
        case LSPExperimentalFeature::DocumentSymbol:
            opts->lspDocumentSymbolEnabled = true;
            break;
        case LSPExperimentalFeature::SignatureHelp:
            opts->lspSignatureHelpEnabled = true;
            break;
    }
}

int LSPWrapper::getTypecheckCount() {
    return lspLoop->getTypecheckCount();
}

} // namespace sorbet::realmain::lsp
