#ifndef RUBY_TYPER_LSP_LSPTYPECHECKER_H
#define RUBY_TYPER_LSP_LSPTYPECHECKER_H

#include "ast/ast.h"
#include "common/concurrency/WorkerPool.h"
#include "common/kvstore/KeyValueStore.h"
#include "core/ErrorQueue.h"
#include "core/NameHash.h"
#include "core/core.h"
#include "main/lsp/LSPConfiguration.h"

namespace sorbet::realmain::lsp {

struct LSPQueryResult {
    std::vector<std::unique_ptr<core::lsp::QueryResponse>> responses;
    // (Optional) Error that occurred during the query that you can pass on to the client.
    std::unique_ptr<ResponseError> error = nullptr;
};

/**
 * Encapsulates an update to LSP's file state in a compact form.
 */
class LSPFileUpdates {
public:
    // This specific update contains edits with id epoch.
    u4 epoch = 0;
    // The total number of edits that this update represents. Used for stats.
    u2 editCount = 0;
    std::vector<std::shared_ptr<core::File>> updatedFiles;
    std::vector<core::FileHash> updatedFileHashes;
    std::vector<ast::ParsedFile> updatedFileIndexes;

    bool canTakeFastPath = false;
    // Indicates that this update contains a new file. Is a hack for determining if combining two updates can take the
    // fast path.
    bool hasNewFiles = false;
    // If true, this update caused a slow path to be canceled.
    bool canceledSlowPath = false;
    // Updated on typechecking thread. Contains indexes processed with typechecking global state.
    std::vector<ast::ParsedFile> updatedFinalGSFileIndexes;
    // (Optional) Updated global state object to use to typecheck this update.
    std::optional<std::unique_ptr<core::GlobalState>> updatedGS;

    // [Tests-only] Used to force block update until canceled / preempted the given number of times.
    SorbetCancellationExpected cancellationExpected = SorbetCancellationExpected::None;
    int preemptionsExpected = 0;

    /**
     * Merges the given (and older) LSPFileUpdates object into this LSPFileUpdates object.
     */
    void mergeInto(const LSPFileUpdates &older);

    /**
     * Returns a copy of this LSPFileUpdates object.
     */
    LSPFileUpdates copy() const;
};

class TypecheckRun final {
public:
    // Errors encountered during typechecking.
    std::vector<std::unique_ptr<core::Error>> errors;
    // The set of files that were typechecked for errors.
    std::vector<core::FileRef> filesTypechecked;
    // The edit applied to `gs`.
    LSPFileUpdates updates;
    // Specifies if the typecheck run took the fast or slow path.
    bool tookFastPath = false;
    // Specifies if the typecheck run was canceled.
    bool canceled = false;
    // If update took the slow path, contains a new global state that should be used moving forward.
    std::optional<std::unique_ptr<core::GlobalState>> newGS;

    TypecheckRun(std::vector<std::unique_ptr<core::Error>> errors = {},
                 std::vector<core::FileRef> filesTypechecked = {}, LSPFileUpdates updates = {},
                 bool tookFastPath = false, std::optional<std::unique_ptr<core::GlobalState>> newGS = std::nullopt);

    // Make a canceled TypecheckRun.
    static TypecheckRun makeCanceled();
};

/**
 * Provides lambdas with a set of operations that they are allowed to do with the LSPTypechecker.
 */
class LSPTypechecker final {
    /**
     * Contains the LSPTypechecker state that is needed to cancel a running slow path operation and any subsequent fast
     * path operations that have preempted it.
     */
    class UndoState final {
    public:
        // Stores the pre-slow-path global state.
        std::unique_ptr<core::GlobalState> gs;
        // Stores index trees containing data stored in `gs` that have been evacuated during the slow path operation.
        UnorderedMap<int, ast::ParsedFile> indexed;
        // Stores file hashes that have been evacuated during the slow path operation.
        UnorderedMap<int, core::FileHash> globalStateHashes;
        // Stores the index trees stored in `gs` that were evacuated because the slow path operation replaced `gs`.
        UnorderedMap<int, ast::ParsedFile> indexedFinalGS;
        // Stores the list of files that had errors before the slow path began.
        std::vector<core::FileRef> filesThatHaveErrors;

        UndoState(std::unique_ptr<core::GlobalState> oldGS, UnorderedMap<int, ast::ParsedFile> oldIndexedFinalGS,
                  std::vector<core::FileRef> oldFilesThatHaveErrors);

        /**
         * Records that the given items were evicted from LSPTypechecker following a typecheck run.
         */
        void recordEvictedState(ast::ParsedFile replacedIndexTree, core::FileHash replacedStateHash);
    };

    /** Contains the ID of the thread responsible for typechecking. */
    std::thread::id typecheckerThreadId;
    /** GlobalState used for typechecking. Mutable because typechecking routines, even when not changing the GlobalState
     * instance, actively consume and replace GlobalState. */
    mutable std::unique_ptr<core::GlobalState> gs;
    /** Trees that have been indexed (with initialGS) and can be reused between different runs */
    std::vector<ast::ParsedFile> indexed;
    /** Trees that have been indexed (with finalGS) and can be reused between different runs */
    UnorderedMap<int, ast::ParsedFile> indexedFinalGS;
    /** Hashes of global states obtained by resolving every file in isolation. Used for fastpath. */
    std::vector<core::FileHash> globalStateHashes;
    /** Stores the epoch in which we last updated the diagnostics for each file. Should be the same length as
     * globalStateHashes. */
    std::vector<u4> diagnosticEpochs;
    /** List of files that have had errors in last run*/
    std::vector<core::FileRef> filesThatHaveErrors;
    std::unique_ptr<KeyValueStore> kvstore; // always null for now.
    /** Set only when typechecking is happening on the slow path. Contains all of the state needed to restore
     * LSPTypechecker to its pre-slow-path state. */
    std::optional<UndoState> cancellationUndoState;

    std::shared_ptr<const LSPConfiguration> config;

    /** Conservatively reruns entire pipeline without caching any trees. Returns 'true' if committed, 'false' if
     * canceled. */
    bool runSlowPath(LSPFileUpdates updates, WorkerPool &workers, bool cancelable, bool preemptible);

    /** Runs incremental typechecking on the provided updates. */
    TypecheckRun runFastPath(LSPFileUpdates updates, WorkerPool &workers) const;

    /** Commits the given updates to SLPTypechecker. */
    void commitFileUpdates(LSPFileUpdates &updates, bool tookFastPath, bool couldBeCanceled);

    /**
     * Sends diagnostics from a typecheck run to the client.
     * `version` specifies the version of the file updates that produced these diagnostics. Used to prevent emitting
     * outdated diagnostics from a slow path run if they had already been re-typechecked on the fast path.
     */
    void pushDiagnostics(u4 epoch, std::vector<core::FileRef> filesTypechecked,
                         std::vector<std::unique_ptr<core::Error>> errors);

    /** Officially 'commits' the output of a `TypecheckRun` by updating the relevant state on LSPTypechecker and sending
     * diagnostics to the editor. */
    void commitTypecheckRun(TypecheckRun run);

    /**
     * Undoes the given slow path changes on LSPTypechecker, and clears the client's error list for any files that were
     * newly introduced with the canceled update. Returns a list of files that need to be retypechecked to update their
     * error lists.
     */
    std::vector<core::FileRef> restore(UndoState &undoState);

    /**
     * Creates an LSPFileUpdates object containing the latest version of the given files (which is a no-op file update).
     */
    LSPFileUpdates getNoopUpdate(std::vector<core::FileRef> frefs) const;

public:
    LSPTypechecker(std::shared_ptr<const LSPConfiguration> config);
    ~LSPTypechecker() = default;

    /**
     * Conducts the first typechecking pass of the session, and initializes `gs`, `index`, and `globalStateHashes`
     * variables. Must be called before typecheck and other functions work.
     *
     * Writes all diagnostic messages to LSPOutput.
     */
    void initialize(LSPFileUpdates updates, WorkerPool &workers);

    /**
     * Typechecks the given input. Returns 'true' if the updates were committed, or 'false' if typechecking was
     * canceled. Distributes work across the given worker pool.
     */
    bool typecheck(LSPFileUpdates updates, WorkerPool &workers);

    /**
     * Typechecks the given input on the fast path. The edit *must* be a fast path edit! Returns 'true' if the updates
     * were committed, or 'false' if typechecking was canceled. Uses a single thread for typechecking.
     */
    void typecheckOnFastPath(LSPFileUpdates updates);

    /**
     * Re-typechecks the provided input to re-produce error messages. Input *must* match already committed state!
     * Provided to facilitate code actions.
     */
    TypecheckRun retypecheck(std::vector<core::FileRef> files) const;

    /** Runs the provided query against the given files, and returns matches. */
    LSPQueryResult query(const core::lsp::Query &q, const std::vector<core::FileRef> &filesForQuery) const;

    /** Runs the provided query against the given files, and returns matches. Runs across the threads in WorkerPool. */
    LSPQueryResult queryMultithreaded(const core::lsp::Query &q, const std::vector<core::FileRef> &filesForQuery,
                                      WorkerPool &workers) const;

    /**
     * Returns the parsed file for the given file, up to the index passes (does not include resolver passes).
     */
    const ast::ParsedFile &getIndexed(core::FileRef fref) const;

    /**
     * Returns the parsed files for the given files, including resolver.
     */
    std::vector<ast::ParsedFile> getResolved(const std::vector<core::FileRef> &frefs) const;

    /**
     * Returns the hashes of all committed files.
     */
    const std::vector<core::FileHash> &getFileHashes() const;

    /**
     * Returns the currently active GlobalState.
     */
    const core::GlobalState &state() const;

    /**
     * Called by LSPTypecheckerCoordinator to indicate that typechecking will occur on the current thread.
     */
    void changeThread();

    /**
     * Returns the typechecker's internal global state, which effectively destroys the typechecker for further use.
     */
    std::unique_ptr<core::GlobalState> destroy();

    /**
     * (Internal use only) Marks the intention to typecheck the given epoch on the slow path.
     */
    void startCommitEpoch(u4 epoch) const;
};

} // namespace sorbet::realmain::lsp
#endif
