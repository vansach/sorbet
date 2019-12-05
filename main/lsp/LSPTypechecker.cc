#include "main/lsp/LSPTypechecker.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "ast/treemap/treemap.h"
#include "common/sort.h"
#include "common/typecase.h"
#include "core/Unfreeze.h"
#include "main/lsp/DefLocSaver.h"
#include "main/lsp/LSPMessage.h"
#include "main/lsp/LSPOutput.h"
#include "main/lsp/LocalVarFinder.h"
#include "main/lsp/LocalVarSaver.h"
#include "main/lsp/ShowOperation.h"
#include "main/pipeline/pipeline.h"

namespace sorbet::realmain::lsp {
using namespace std;

namespace {
vector<string> frefsToPaths(const core::GlobalState &gs, const vector<core::FileRef> &refs) {
    vector<string> paths;
    paths.resize(refs.size());
    std::transform(refs.begin(), refs.end(), paths.begin(),
                   [&gs](const auto &ref) -> string { return string(ref.data(gs).path()); });
    return paths;
}

void sendTypecheckInfo(const LSPConfiguration &config, const core::GlobalState &gs, SorbetTypecheckRunStatus status,
                       bool isFastPath, std::vector<core::FileRef> filesTypechecked) {
    if (config.getClientConfig().enableTypecheckInfo) {
        vector<string> filePaths;
        for (auto &file : filesTypechecked) {
            filePaths.emplace_back(file.data(gs).path());
        }
        auto sorbetTypecheckInfo = make_unique<SorbetTypecheckRunInfo>(status, isFastPath, filePaths);
        config.output->write(make_unique<LSPMessage>(
            make_unique<NotificationMessage>("2.0", LSPMethod::SorbetTypecheckRunInfo, move(sorbetTypecheckInfo))));
    }
}
} // namespace

LSPTypechecker::UndoState::UndoState(unique_ptr<core::GlobalState> oldGS,
                                     UnorderedMap<int, ast::ParsedFile> oldIndexedFinalGS,
                                     vector<core::FileRef> oldFilesThatHaveErrors)
    : gs(move(oldGS)), indexedFinalGS(std::move(oldIndexedFinalGS)), filesThatHaveErrors(move(oldFilesThatHaveErrors)) {
}

vector<core::FileRef> LSPTypechecker::restore(UndoState &undoState) {
    // Replace indexed trees and file hashes for any files that have been typechecked on the new final GS.
    for (auto &entry : indexedFinalGS) {
        // TODO(jvilk): indexedFinalGS contains a bunch of trees that haven't actually been updated, but were
        // re-typechecked due to changes in other files (e.g. sig updates). We ignore them here, but it would be nice to
        // amend the fast path to not commit these updates at all.
        if (undoState.indexed.contains(entry.first)) {
            indexed[entry.first] = move(undoState.indexed[entry.first]);
            ENFORCE(undoState.globalStateHashes.contains(entry.first));
            globalStateHashes[entry.first] = move(undoState.globalStateHashes[entry.first]);
        }
    }
    indexedFinalGS = std::move(undoState.indexedFinalGS);

    // Clear errors for files that are in the new set of files with errors but not the old set.
    // TODO: Update with the reverse when we switch to tombstoning files.
    vector<string> newPathsThatHaveErrors = frefsToPaths(*gs, filesThatHaveErrors);
    vector<string> oldPathsThatHaveErrors = frefsToPaths(*undoState.gs, undoState.filesThatHaveErrors);
    fast_sort(newPathsThatHaveErrors);
    fast_sort(oldPathsThatHaveErrors);
    vector<string> clearErrorsFor;
    std::set_difference(newPathsThatHaveErrors.begin(), newPathsThatHaveErrors.end(), oldPathsThatHaveErrors.begin(),
                        oldPathsThatHaveErrors.end(), std::inserter(clearErrorsFor, clearErrorsFor.begin()));
    for (auto &file : clearErrorsFor) {
        vector<unique_ptr<Diagnostic>> diagnostics;
        config->output->write(make_unique<LSPMessage>(make_unique<NotificationMessage>(
            "2.0", LSPMethod::TextDocumentPublishDiagnostics,
            make_unique<PublishDiagnosticsParams>(config->localName2Remote(file), move(diagnostics)))));
    }
    filesThatHaveErrors = undoState.filesThatHaveErrors;

    // Finally, restore the old global state.
    gs = move(undoState.gs);
    // Inform caller that we need to retypecheck all files that used to have errors.
    return undoState.filesThatHaveErrors;
}

void LSPTypechecker::UndoState::recordEvictedState(ast::ParsedFile replacedIndexTree,
                                                   core::FileHash replacedStateHash) {
    const auto id = replacedIndexTree.file.id();
    // The first time a file gets evicted, it's an index tree from the old global state.
    // Subsequent times it is evicting old index trees from the new global state, and we don't care.
    // Also, ignore updates to new files (id >= size of file table)
    if (id < gs->getFiles().size() && !indexed.contains(id)) {
        indexed[id] = move(replacedIndexTree);
        // gsh should be in-sync with indexed.
        ENFORCE(!globalStateHashes.contains(id));
        globalStateHashes[id] = move(replacedStateHash);
    }
}

LSPTypechecker::LSPTypechecker(shared_ptr<const LSPConfiguration> config)
    : typecheckerThreadId(this_thread::get_id()), config(move(config)) {}

void LSPTypechecker::initialize(LSPFileUpdates updates, WorkerPool &workers) {
    globalStateHashes = move(updates.updatedFileHashes);
    indexed = move(updates.updatedFileIndexes);
    // Initialize to all zeroes.
    diagnosticEpochs = vector<u4>(globalStateHashes.size(), 0);
    ENFORCE(globalStateHashes.size() == indexed.size());

    // Initialization typecheck is not cancelable, but is preemptible.
    auto committed = runSlowPath(move(updates), workers, /* cancelable */ false, /* preemptible */ true);
    ENFORCE(committed);
}

bool LSPTypechecker::typecheck(LSPFileUpdates updates, WorkerPool &workers) {
    vector<core::FileRef> addToTypecheck;
    if (updates.canceledSlowPath) {
        // This update canceled the last slow path, so we should have undo state to restore to go to the point _before_
        // that slow path. This should always be the case, but let's not crash release builds.
        ENFORCE(cancellationUndoState.has_value());
        if (cancellationUndoState.has_value()) {
            // This is the typecheck that caused us to cancel the previous slow path. Un-commit all typechecker changes.
            auto oldFilesWithErrors = restore(cancellationUndoState.value());
            cancellationUndoState = nullopt;
            // Retypecheck any files returned by restore that aren't already in this update.
            vector<core::FileRef> newUpdatedFiles;
            for (auto &tree : updates.updatedFileIndexes) {
                newUpdatedFiles.push_back(tree.file);
            }
            fast_sort(newUpdatedFiles);
            fast_sort(oldFilesWithErrors);
            std::set_difference(oldFilesWithErrors.begin(), oldFilesWithErrors.end(), newUpdatedFiles.begin(),
                                newUpdatedFiles.end(), std::inserter(addToTypecheck, addToTypecheck.begin()));
        }
    }

    vector<core::FileRef> filesTypechecked;
    bool committed = true;
    const bool isFastPath = updates.canTakeFastPath;
    sendTypecheckInfo(*config, *gs, SorbetTypecheckRunStatus::Started, isFastPath, {});
    if (updates.canTakeFastPath) {
        // Retypecheck all files that formerly had errors.
        for (auto fref : addToTypecheck) {
            auto &index = getIndexed(fref);
            updates.updatedFileIndexes.push_back({index.tree->deepCopy(), index.file});
            updates.updatedFiles.push_back(gs->getFiles()[fref.id()]);
            updates.updatedFileHashes.push_back(globalStateHashes[fref.id()]);
        }
        auto run = runFastPath(move(updates), workers);
        filesTypechecked = run.filesTypechecked;
        commitTypecheckRun(move(run));
    } else {
        // No need to add any files to typecheck, as it'll retypecheck all files.
        // TODO: In future, have it prioritize old files with errors.
        committed = runSlowPath(move(updates), workers, /* cancelable */ true, /* preemptible */ true);
        // Note: filesTypechecked isn't important for slow path typecheck info; the slow path typechecks everything.
    }
    sendTypecheckInfo(*config, *gs, committed ? SorbetTypecheckRunStatus::Ended : SorbetTypecheckRunStatus::Cancelled,
                      isFastPath, move(filesTypechecked));
    return committed;
}

void LSPTypechecker::typecheckOnFastPath(LSPFileUpdates updates) {
    if (!updates.canTakeFastPath) {
        Exception::raise("Tried to typecheck a slow path edit on the fast path.");
    }
    auto workers = WorkerPool::create(0, *config->logger);
    auto committed = typecheck(move(updates), *workers);
    // Fast path edits can't be canceled.
    ENFORCE(committed);
}

TypecheckRun LSPTypechecker::runFastPath(LSPFileUpdates updates, WorkerPool &workers) const {
    ENFORCE(this_thread::get_id() == typecheckerThreadId,
            "runTypechecking can only be called from the typechecker thread.");
    // We assume gs is a copy of initialGS, which has had the inferencer & resolver run.
    ENFORCE(gs->lspTypecheckCount > 0,
            "Tried to run fast path with a GlobalState object that never had inferencer and resolver runs.");
    // This property is set to 'true' in tests only if the update is expected to take the slow path and get cancelled.
    ENFORCE(updates.cancellationExpected == SorbetCancellationExpected::None);
    ENFORCE(updates.preemptionsExpected == 0);
    // This path only works for fast path updates.
    ENFORCE(updates.canTakeFastPath);

    Timer timeit(config->logger, "fast_path");
    vector<core::FileRef> subset;
    vector<core::NameHash> changedHashes;
    {
        const auto &hashes = updates.updatedFileHashes;
        ENFORCE(updates.updatedFiles.size() == hashes.size());

        int i = -1;
        for (auto &f : updates.updatedFiles) {
            ++i;
            auto fref = gs->findFileByPath(f->path());
            // We don't support new files on the fast path. This enforce failing indicates a bug in our fast/slow
            // path logic in LSPPreprocessor.
            ENFORCE(fref.exists());
            if (fref.exists()) {
                // Update to existing file on fast path
                auto &oldHash = globalStateHashes[fref.id()];
                for (auto &p : hashes[i].definitions.methodHashes) {
                    auto fnd = oldHash.definitions.methodHashes.find(p.first);
                    ENFORCE(fnd != oldHash.definitions.methodHashes.end(), "definitionHash should have failed");
                    if (fnd->second != p.second) {
                        changedHashes.emplace_back(p.first);
                    }
                }
                gs = core::GlobalState::replaceFile(move(gs), fref, f);
                // If file doesn't have a typed: sigil, then we need to ensure it's typechecked using typed: false.
                fref.data(*gs).strictLevel = pipeline::decideStrictLevel(*gs, fref, config->opts);
                subset.emplace_back(fref);
            }
        }
        core::NameHash::sortAndDedupe(changedHashes);
    }

    int i = -1;
    for (auto &oldHash : globalStateHashes) {
        i++;
        vector<core::NameHash> intersection;
        std::set_intersection(changedHashes.begin(), changedHashes.end(), oldHash.usages.sends.begin(),
                              oldHash.usages.sends.end(), std::back_inserter(intersection));
        if (!intersection.empty()) {
            auto ref = core::FileRef(i);
            config->logger->debug("Added {} to update set as used a changed method",
                                  !ref.exists() ? "" : ref.data(*gs).path());
            subset.emplace_back(ref);
        }
    }
    // Remove any duplicate files.
    fast_sort(subset);
    subset.resize(std::distance(subset.begin(), std::unique(subset.begin(), subset.end())));

    prodCategoryCounterInc("lsp.updates", "fastpath");
    config->logger->debug("Taking fast path");
    ENFORCE(gs->errorQueue->isEmpty());
    vector<ast::ParsedFile> updatedIndexed;
    for (auto &f : subset) {
        unique_ptr<KeyValueStore> kvstore; // nullptr
        // TODO: Thread through kvstore.
        ENFORCE(this->kvstore == nullptr);
        auto t = pipeline::indexOne(config->opts, *gs, f, kvstore);
        updatedIndexed.emplace_back(ast::ParsedFile{t.tree->deepCopy(), t.file});
        updates.updatedFinalGSFileIndexes.push_back(move(t));
    }

    ENFORCE(gs->lspQuery.isEmpty());
    auto resolved = pipeline::incrementalResolve(*gs, move(updatedIndexed), config->opts);
    pipeline::typecheck(gs, move(resolved), config->opts, workers);
    auto out = gs->errorQueue->drainWithQueryResponses();
    gs->lspTypecheckCount++;
    return TypecheckRun(move(out.first), move(subset), move(updates), true);
}

namespace {
pair<unique_ptr<core::GlobalState>, ast::ParsedFile>
updateFile(unique_ptr<core::GlobalState> gs, const shared_ptr<core::File> &file, const options::Options &opts) {
    core::FileRef fref = gs->findFileByPath(file->path());
    if (fref.exists()) {
        gs = core::GlobalState::replaceFile(move(gs), fref, file);
    } else {
        fref = gs->enterFile(file);
    }
    fref.data(*gs).strictLevel = pipeline::decideStrictLevel(*gs, fref, opts);
    std::unique_ptr<KeyValueStore> kvstore; // nullptr
    return make_pair(move(gs), pipeline::indexOne(opts, *gs, fref, kvstore));
}
} // namespace

bool LSPTypechecker::runSlowPath(LSPFileUpdates updates, WorkerPool &workers, bool cancelable, bool preemptible) {
    ENFORCE(this_thread::get_id() == typecheckerThreadId,
            "runSlowPath can only be called from the typechecker thread.");

    auto &logger = config->logger;
    ShowOperation slowPathOp(*config, "SlowPath", "Typechecking...");
    Timer timeit(logger, "slow_path");
    ENFORCE(!updates.canTakeFastPath || config->disableFastPath);
    ENFORCE(updates.updatedGS.has_value());
    if (!updates.updatedGS.has_value()) {
        Exception::raise("runSlowPath called with an update that lacks an updated global state.");
    }
    logger->debug("Taking slow path");

    vector<core::FileRef> affectedFiles;
    auto finalGS = move(updates.updatedGS.value());
    // Replace error queue with one that is owned by this thread.
    finalGS->errorQueue = make_shared<core::ErrorQueue>(finalGS->errorQueue->logger, finalGS->errorQueue->tracer);
    finalGS->errorQueue->ignoreFlushes = true;
    // Note: Commits can only be canceled if this edit is cancelable, LSP is running across multiple threads, and the
    // cancelation feature is enabled.
    const bool committed = finalGS->tryCommitEpoch(updates.epoch, cancelable, [&]() -> void {
        vector<ast::ParsedFile> indexedCopies;
        UnorderedSet<int> updatedFiles;
        // Index the updated files using finalGS.
        {
            core::UnfreezeFileTable fileTableAccess(*finalGS);
            for (auto &file : updates.updatedFiles) {
                auto pair = updateFile(move(finalGS), file, config->opts);
                finalGS = move(pair.first);
                auto &ast = pair.second;
                if (ast.tree) {
                    indexedCopies.emplace_back(ast::ParsedFile{ast.tree->deepCopy(), ast.file});
                    updatedFiles.insert(ast.file.id());
                }
                updates.updatedFinalGSFileIndexes.push_back(move(ast));
            }
        }
        // Before making preemption or cancelation possible, pre-commit the changes from this slow path so that
        // preempted queries can use them and the code after this lambda can assume that this step happened.
        updates.updatedGS = move(finalGS);
        commitFileUpdates(updates, false, cancelable);

        // Copy the indexes of unchanged files.
        for (const auto &tree : indexed) {
            // Note: indexed entries for payload files don't have any contents.
            if (tree.tree && !updatedFiles.contains(tree.file.id())) {
                indexedCopies.emplace_back(ast::ParsedFile{tree.tree->deepCopy(), tree.file});
            }
        }

        if (gs->wasTypecheckingCanceled()) {
            return;
        }

        ENFORCE(gs->lspQuery.isEmpty());
        if (gs->sleepInSlowPath) {
            // Let cancelation occur during the sleep.
            for (int i = 0; i < 200; i++) {
                // 200*10 = 2000ms
                Timer::timedSleep(10ms, *logger, "slow_path.resolve.sleep");
                if (gs->wasTypecheckingCanceled()) {
                    return;
                }
            }
        }
        auto maybeResolved = pipeline::resolve(gs, move(indexedCopies), config->opts, workers, config->skipConfigatron);
        if (!maybeResolved.hasResult()) {
            return;
        }

        auto &resolved = maybeResolved.result();
        for (auto &tree : resolved) {
            ENFORCE(tree.file.exists());
            affectedFiles.push_back(tree.file);
        }

        // [TESTS ONLY] If a cancellation was expected, wait for it to happen.
        if (updates.cancellationExpected == SorbetCancellationExpected::BeforeResolver) {
            while (!gs->wasTypecheckingCanceled()) {
                Timer::timedSleep(1ms, *logger, "slow_path.expected_cancellation.sleep");
            }
            return;
        }

        // Inform the fast path that this global state is OK for typechecking as resolution has completed.
        gs->lspTypecheckCount++;

        // (Tests only) Let all planned preemptions happen now (rather than later, when we cannot count them)
        while (updates.preemptionsExpected > 0) {
            while (!gs->tryRunPreemptionTask()) {
                Timer::timedSleep(1ms, *logger, "slow_path.expected_preemption.sleep");
            }
            updates.preemptionsExpected--;
        }

        if (gs->sleepInSlowPath) {
            // Let preemption and cancelation occur during the sleep.
            for (int i = 0; i < 1000; i++) {
                // 1000*10 = 10000ms
                Timer::timedSleep(10ms, *logger, "slow_path.typecheck.sleep");
                gs->tryRunPreemptionTask();
                if (gs->wasTypecheckingCanceled()) {
                    return;
                }
            }
        }

        // [TESTS ONLY] If a cancellation was expected, wait for it to happen.
        if (updates.cancellationExpected == SorbetCancellationExpected::AfterResolver) {
            while (!gs->wasTypecheckingCanceled()) {
                Timer::timedSleep(1ms, *logger, "slow_path.expected_cancellation.sleep");
            }
            return;
        }

        pipeline::typecheck(gs, move(resolved), config->opts, workers, cancelable, preemptible);
    });

    // Note: This is important to do even if the slow path was canceled. It clears out any typechecking errors from the
    // aborted typechecking run.
    auto out = gs->errorQueue->drainWithQueryResponses();
    gs->lspQuery = core::lsp::Query::noQuery();

    if (committed) {
        prodCategoryCounterInc("lsp.updates", "slowpath");
        // No need to keep around cancelation state!
        cancellationUndoState = nullopt;
        pushDiagnostics(updates.epoch, move(affectedFiles), move(out.first));
        return true;
    } else {
        prodCategoryCounterInc("lsp.updates", "slowpath_canceled");
        // Update responsible will use state in `cancellationUndoState` to restore typechecker to the point before
        // this slow path.
        ENFORCE(cancelable);
        return false;
    }
}

void LSPTypechecker::commitFileUpdates(LSPFileUpdates &updates, bool tookFastPath, bool couldBeCanceled) {
    // Only take the fast path if the updates _can_ take the fast path.
    ENFORCE((tookFastPath && updates.canTakeFastPath) || !tookFastPath);
    // The fast path cannot be canceled.
    ENFORCE(!(tookFastPath && couldBeCanceled));
    if (couldBeCanceled) {
        ENFORCE(updates.updatedGS.has_value());
        cancellationUndoState = make_optional<UndoState>(move(gs), std::move(indexedFinalGS), filesThatHaveErrors);
    }
    // Clear out state associated with old finalGS.
    if (!tookFastPath) {
        indexedFinalGS.clear();
    }

    int i = -1;
    ENFORCE(updates.updatedFileIndexes.size() == updates.updatedFileHashes.size() &&
            updates.updatedFileHashes.size() == updates.updatedFiles.size());
    ENFORCE(globalStateHashes.size() == indexed.size() && globalStateHashes.size() == diagnosticEpochs.size());
    for (auto &ast : updates.updatedFileIndexes) {
        i++;
        const int id = ast.file.id();
        if (id >= indexed.size()) {
            // New file
            indexed.resize(id + 1);
            globalStateHashes.resize(id + 1);
            diagnosticEpochs.resize(id + 1);
        }
        if (cancellationUndoState.has_value()) {
            cancellationUndoState->recordEvictedState(move(indexed[id]), move(globalStateHashes[id]));
        }
        indexed[id] = move(ast);
        globalStateHashes[id] = move(updates.updatedFileHashes[i]);
    }

    for (auto &ast : updates.updatedFinalGSFileIndexes) {
        indexedFinalGS[ast.file.id()] = move(ast);
    }

    if (updates.updatedGS.has_value()) {
        gs = move(updates.updatedGS.value());
    } else {
        ENFORCE(tookFastPath);
    }
}

void LSPTypechecker::pushDiagnostics(u4 epoch, vector<core::FileRef> filesTypechecked,
                                     vector<std::unique_ptr<core::Error>> errors) {
    vector<core::FileRef> errorFilesInNewRun;
    UnorderedMap<core::FileRef, vector<std::unique_ptr<core::Error>>> errorsAccumulated;

    // Update epochs of all files that were typechecked.
    for (auto f : filesTypechecked) {
        // TODO(jvilk): Overflow could theoretically happen. It would take an absurdly long time for someone to make
        // 4294967295 edits in one session. One way to handle that case: Have a special overflow request that blocks
        // preemption and resets all versions to 0.
        if (diagnosticEpochs[f.id()] < epoch) {
            diagnosticEpochs[f.id()] = epoch;
        }
    }

    for (auto &e : errors) {
        if (e->isSilenced) {
            continue;
        }
        auto file = e->loc.file();
        errorsAccumulated[file].emplace_back(std::move(e));
    }

    for (auto &accumulated : errorsAccumulated) {
        // Ignore errors from files that have been typechecked on newer versions (e.g. because they preempted the slow
        // path)
        // TODO(jvilk): See overflow comment above.
        if (diagnosticEpochs[accumulated.first.id()] <= epoch) {
            errorFilesInNewRun.push_back(accumulated.first);
        }
    }

    vector<core::FileRef> filesToUpdateErrorListFor = errorFilesInNewRun;

    UnorderedSet<core::FileRef> filesTypecheckedAsSet;
    filesTypecheckedAsSet.insert(filesTypechecked.begin(), filesTypechecked.end());

    for (auto f : this->filesThatHaveErrors) {
        // TODO(jvilk): Overflow warning applies here, too.
        if (filesTypecheckedAsSet.find(f) != filesTypecheckedAsSet.end() && diagnosticEpochs[f.id()] <= epoch) {
            // We've retypechecked this file, it hasn't been typechecked with newer edits, and it doesn't have errors.
            // thus, we will update the error list for this file on client to be the empty list.
            filesToUpdateErrorListFor.push_back(f);
        } else {
            // We either did not retypecheck this file, _or_ it has since been typechecked with newer edits.
            // We need to remember that it had error.
            errorFilesInNewRun.push_back(f);
        }
    }

    fast_sort(filesToUpdateErrorListFor);
    filesToUpdateErrorListFor.erase(unique(filesToUpdateErrorListFor.begin(), filesToUpdateErrorListFor.end()),
                                    filesToUpdateErrorListFor.end());

    fast_sort(errorFilesInNewRun);
    errorFilesInNewRun.erase(unique(errorFilesInNewRun.begin(), errorFilesInNewRun.end()), errorFilesInNewRun.end());

    this->filesThatHaveErrors = errorFilesInNewRun;

    for (auto file : filesToUpdateErrorListFor) {
        if (file.exists()) {
            string uri;
            { // uri
                if (file.data(*gs).sourceType == core::File::Type::Payload) {
                    uri = string(file.data(*gs).path());
                } else {
                    uri = config->fileRef2Uri(*gs, file);
                }
            }

            vector<unique_ptr<Diagnostic>> diagnostics;
            {
                // diagnostics
                if (errorsAccumulated.find(file) != errorsAccumulated.end()) {
                    for (auto &e : errorsAccumulated[file]) {
                        auto range = Range::fromLoc(*gs, e->loc);
                        if (range == nullptr) {
                            continue;
                        }
                        auto diagnostic = make_unique<Diagnostic>(std::move(range), e->header);
                        diagnostic->code = e->what.code;
                        diagnostic->severity = DiagnosticSeverity::Error;

                        typecase(e.get(), [&](core::Error *ce) {
                            vector<unique_ptr<DiagnosticRelatedInformation>> relatedInformation;
                            for (auto &section : ce->sections) {
                                string sectionHeader = section.header;

                                for (auto &errorLine : section.messages) {
                                    string message;
                                    if (errorLine.formattedMessage.length() > 0) {
                                        message = errorLine.formattedMessage;
                                    } else {
                                        message = sectionHeader;
                                    }
                                    auto location = config->loc2Location(*gs, errorLine.loc);
                                    if (location == nullptr) {
                                        continue;
                                    }
                                    relatedInformation.push_back(
                                        make_unique<DiagnosticRelatedInformation>(std::move(location), message));
                                }
                            }
                            // Add link to error documentation.
                            relatedInformation.push_back(make_unique<DiagnosticRelatedInformation>(
                                make_unique<Location>(
                                    absl::StrCat(config->opts.errorUrlBase, e->what.code),
                                    make_unique<Range>(make_unique<Position>(0, 0), make_unique<Position>(0, 0))),
                                "Click for more information on this error."));
                            diagnostic->relatedInformation = move(relatedInformation);
                        });
                        diagnostics.push_back(move(diagnostic));
                    }
                }
            }

            config->output->write(make_unique<LSPMessage>(
                make_unique<NotificationMessage>("2.0", LSPMethod::TextDocumentPublishDiagnostics,
                                                 make_unique<PublishDiagnosticsParams>(uri, move(diagnostics)))));
        }
    }
}

void LSPTypechecker::commitTypecheckRun(TypecheckRun run) {
    auto &logger = config->logger;

    if (run.canceled) {
        logger->debug("[Typechecker] Typecheck run for epoch {} was canceled.", run.updates.epoch);
        return;
    }

    Timer timeit(logger, "commitTypecheckRun");
    commitFileUpdates(run.updates, run.tookFastPath, false);
    pushDiagnostics(run.updates.epoch, move(run.filesTypechecked), move(run.errors));
}

unique_ptr<core::GlobalState> LSPTypechecker::destroy() {
    return move(gs);
}

namespace {
void tryApplyLocalVarSaver(const core::GlobalState &gs, vector<ast::ParsedFile> &indexedCopies) {
    if (gs.lspQuery.kind != core::lsp::Query::Kind::VAR) {
        return;
    }
    for (auto &t : indexedCopies) {
        LocalVarSaver localVarSaver;
        core::Context ctx(gs, core::Symbols::root());
        t.tree = ast::TreeMap::apply(ctx, localVarSaver, move(t.tree));
    }
}

void tryApplyDefLocSaver(const core::GlobalState &gs, vector<ast::ParsedFile> &indexedCopies) {
    if (gs.lspQuery.kind != core::lsp::Query::Kind::LOC && gs.lspQuery.kind != core::lsp::Query::Kind::SYMBOL) {
        return;
    }
    for (auto &t : indexedCopies) {
        DefLocSaver defLocSaver;
        core::Context ctx(gs, core::Symbols::root());
        t.tree = ast::TreeMap::apply(ctx, defLocSaver, move(t.tree));
    }
}
} // namespace

LSPQueryResult LSPTypechecker::query(const core::lsp::Query &q, const std::vector<core::FileRef> &filesForQuery) const {
    auto workers = WorkerPool::create(0, *config->logger);
    return queryMultithreaded(q, filesForQuery, *workers);
}

LSPQueryResult LSPTypechecker::queryMultithreaded(const core::lsp::Query &q,
                                                  const std::vector<core::FileRef> &filesForQuery,
                                                  WorkerPool &workers) const {
    // We assume gs is a copy of initialGS, which has had the inferencer & resolver run.
    ENFORCE(gs->lspTypecheckCount > 0,
            "Tried to run a query with a GlobalState object that never had inferencer and resolver runs.");

    Timer timeit(config->logger, "query");
    prodCategoryCounterInc("lsp.updates", "query");
    ENFORCE(gs->errorQueue->isEmpty());
    ENFORCE(gs->lspQuery.isEmpty());
    gs->lspQuery = q;
    auto resolved = getResolved(filesForQuery);
    tryApplyDefLocSaver(*gs, resolved);
    tryApplyLocalVarSaver(*gs, resolved);
    pipeline::typecheck(gs, move(resolved), config->opts, workers);
    auto out = gs->errorQueue->drainWithQueryResponses();
    gs->lspTypecheckCount++;
    gs->lspQuery = core::lsp::Query::noQuery();
    return LSPQueryResult{move(out.second)};
}

TypecheckRun LSPTypechecker::retypecheck(LSPFileUpdates updates) const {
    if (!updates.canTakeFastPath) {
        Exception::raise("Tried to typecheck slow path updates with retypecheck. Retypecheck can only typecheck the "
                         "previously typechecked version of a file.");
    }

    for (const auto &file : updates.updatedFiles) {
        auto path = file->path();
        auto source = file->source();
        auto fref = gs->findFileByPath(path);
        if (!fref.exists() || fref.data(*gs).source() != source) {
            Exception::raise("Retypecheck can only typecheck the previously typechecked version of a file.");
        }
    }

    auto workers = WorkerPool::create(0, *config->logger);
    return runFastPath(move(updates), *workers);
}

const ast::ParsedFile &LSPTypechecker::getIndexed(core::FileRef fref) const {
    const auto id = fref.id();
    auto treeFinalGS = indexedFinalGS.find(id);
    if (treeFinalGS != indexedFinalGS.end()) {
        return treeFinalGS->second;
    }
    ENFORCE(id < indexed.size());
    return indexed[id];
}

vector<ast::ParsedFile> LSPTypechecker::getResolved(const vector<core::FileRef> &frefs) const {
    vector<ast::ParsedFile> updatedIndexed;
    for (auto fref : frefs) {
        auto &indexed = getIndexed(fref);
        if (indexed.tree) {
            updatedIndexed.emplace_back(ast::ParsedFile{indexed.tree->deepCopy(), indexed.file});
        }
    }
    return pipeline::incrementalResolve(*gs, move(updatedIndexed), config->opts);
}

const std::vector<core::FileHash> &LSPTypechecker::getFileHashes() const {
    return globalStateHashes;
}

const core::GlobalState &LSPTypechecker::state() const {
    return *gs;
}

void LSPTypechecker::changeThread() {
    auto newId = this_thread::get_id();
    ENFORCE(newId != typecheckerThreadId);
    typecheckerThreadId = newId;
}

TypecheckRun::TypecheckRun(vector<unique_ptr<core::Error>> errors, vector<core::FileRef> filesTypechecked,
                           LSPFileUpdates updates, bool tookFastPath,
                           std::optional<std::unique_ptr<core::GlobalState>> newGS)
    : errors(move(errors)), filesTypechecked(move(filesTypechecked)), updates(move(updates)),
      tookFastPath(tookFastPath), newGS(move(newGS)) {}

TypecheckRun TypecheckRun::makeCanceled() {
    TypecheckRun run;
    run.canceled = true;
    return run;
}

} // namespace sorbet::realmain::lsp
