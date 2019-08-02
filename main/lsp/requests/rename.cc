#include "absl/strings/match.h"
#include "core/lsp/QueryResponse.h"
#include "main/lsp/lsp.h"

using namespace std;

namespace sorbet::realmain::lsp {

LSPWorkspaceEdit LSPLoop::getRenameEdits(unique_ptr<core::GlobalState> gs, core::SymbolRef symbol) {
    vector<unique_ptr<Location>> references;
    auto refResult = getReferencesToSymbol(move(gs), symbol);
    gs = move(refResult.gs);

    auto we = make_unique<WorkspaceEdit>();
    vector<unique_ptr<TextDocumentEdit>> edits;
    for (auto loc : refResult.locs) {
        if (loc.exists()) {
            // Get source text at location.
        }
    }

    // showFullName contains full :: name.
    symbol.data(*gs)->name.toString(*gs);
    symbol.data(*gs)->toString(*gs);

    return LSPWorkspaceEdit{move(gs), move(we)};
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
                auto result = getRenameEdits(move(gs), constResp->symbol);
                gs = move(result.gs);
                response->result = move(result.edit);
            } else if (auto defResp = resp->isDefinition()) {
                if (defResp->symbol.data(*gs)->isClass()) {
                    auto result = getRenameEdits(move(gs), defResp->symbol);
                    gs = move(result.gs);
                    response->result = move(result.edit);
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
