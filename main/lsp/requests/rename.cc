#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "core/lsp/QueryResponse.h"
#include "main/lsp/lsp.h"

using namespace std;

namespace sorbet::realmain::lsp {

core::Loc range2Loc(const core::GlobalState &gs, const Range &range, core::FileRef file) {
    ENFORCE(range.start->line >= 0);
    ENFORCE(range.start->character >= 0);
    ENFORCE(range.end->line >= 0);
    ENFORCE(range.end->character >= 0);

    auto start = core::Loc::pos2Offset(file.data(gs),
                                       core::Loc::Detail{(u4)range.start->line + 1, (u4)range.start->character + 1});
    auto end =
        core::Loc::pos2Offset(file.data(gs), core::Loc::Detail{(u4)range.end->line + 1, (u4)range.end->character + 1});

    return core::Loc(file, start, end);
}

pair<unique_ptr<core::GlobalState>, unique_ptr<WorkspaceEdit>>
LSPLoop::getRenameEdits(unique_ptr<core::GlobalState> gs, core::SymbolRef symbol, std::string_view newName) {
    vector<unique_ptr<Location>> references;
    tie(gs, references) = getReferencesToSymbol(move(gs), symbol, move(references));

    auto originalName = symbol.data(*gs)->name.toString(*gs);
    auto we = make_unique<WorkspaceEdit>();
    // TextEdit
    UnorderedMap<string, vector<unique_ptr<TextEdit>>> edits;
    for (auto &location : references) {
        // Get text at location.
        // TODO: Not payload files...?
        auto fref = uri2FileRef(location->uri);
        if (fref.data(*gs).isPayload()) {
            // We don't support renaming things in payload files.
            // TODO: Error?
            continue;
        }
        auto loc = range2Loc(*gs, *location->range, fref);
        auto source = loc.source(*gs);
        std::vector<std::string> strs = absl::StrSplit(source, "::");
        strs[strs.size() - 1] = string(newName);
        edits[location->uri].push_back(make_unique<TextEdit>(move(location->range), absl::StrJoin(strs, "::")));
    }

    vector<unique_ptr<TextDocumentEdit>> textDocEdits;
    for (auto &item : edits) {
        // TODO: Version.
        textDocEdits.push_back(make_unique<TextDocumentEdit>(
            make_unique<VersionedTextDocumentIdentifier>(item.first, JSONNullObject()), move(item.second)));
    }
    we->documentChanges = move(textDocEdits);

    return make_pair(move(gs), move(we));
}

std::optional<std::string> validateName(std::string_view name) {
    // Name must be a valid Ruby identifier.
    // TODO: Validate.
    return nullopt;
}

LSPResult LSPLoop::handleTextDocumentRename(unique_ptr<core::GlobalState> gs, const MessageId &id,
                                            const RenameParams &params) {
    auto response = make_unique<ResponseMessage>("2.0", id, LSPMethod::TextDocumentRename);
    if (!opts.lspRenameEnabled) {
        response->error = make_unique<ResponseError>(
            (int)LSPErrorCodes::InvalidRequest, "The `Rename` LSP feature is experimental and disabled by default.");
        return LSPResult::make(move(gs), move(response));
    }

    prodCategoryCounterInc("lsp.messages.processed", "textDocument.rename");
    // Sanity check the text.
    if (auto validationError = validateName(params.newName)) {
        response->error = make_unique<ResponseError>((int)LSPErrorCodes::InvalidRequest, *validationError);
        return LSPResult::make(move(gs), move(response));
    }

    ShowOperation op(*this, "References", "Renaming...");

    auto result = setupLSPQueryByLoc(move(gs), params.textDocument->uri, *params.position,
                                     LSPMethod::TextDocumentCompletion, false);
    if (auto run1 = get_if<TypecheckRun>(&result)) {
        gs = move(run1->gs);
        // An explicit null indicates that we don't support this request (or that nothing was at the location).
        // Note: Need to correctly type variant here so it goes into right 'slot' of result variant.
        response->result = variant<JSONNullObject, unique_ptr<WorkspaceEdit>>(JSONNullObject());
        auto &queryResponses = run1->responses;
        if (!queryResponses.empty()) {
            auto resp = move(queryResponses[0]);
            // Only supports rename requests from constants and class definitions.
            if (auto constResp = resp->isConstant()) {
                tie(gs, response->result) = getRenameEdits(move(gs), constResp->symbol, params.newName);
            } else if (auto defResp = resp->isDefinition()) {
                if (defResp->symbol.data(*gs)->isClass()) {
                    tie(gs, response->result) = getRenameEdits(move(gs), defResp->symbol, params.newName);
                }
            }
        }
    } else if (auto error = get_if<pair<unique_ptr<ResponseError>, unique_ptr<core::GlobalState>>>(&result)) {
        // An error happened while setting up the query.
        response->error = move(error->first);
        gs = move(error->second);
    } else {
        // Should never happen, but satisfy the compiler.
        ENFORCE(false, "Internal error: setupLSPQueryByLoc returned invalid value.");
    }
    return LSPResult::make(move(gs), move(response));
}

} // namespace sorbet::realmain::lsp
