#include "absl/strings/match.h"
#include "core/lsp/QueryResponse.h"
#include "main/lsp/lsp.h"

using namespace std;

namespace sorbet::realmain::lsp {

unique_ptr<PrepareRenameResult> getPrepareRenameResult(const core::GlobalState &gs, core::SymbolRef symbol) {
    auto range = loc2Range(gs, symbol.data(gs)->loc());
    auto result = make_unique<PrepareRenameResult>(move(range));
    result->placeholder = symbol.data(gs)->name.toString(gs);
    return result;
}

LSPResult LSPLoop::handleTextDocumentPrepareRename(unique_ptr<core::GlobalState> gs, const MessageId &id,
                                                   const TextDocumentPositionParams &params) {
    auto response = make_unique<ResponseMessage>("2.0", id, LSPMethod::TextDocumentPrepareRename);
    if (!opts.lspRenameEnabled) {
        response->error = make_unique<ResponseError>(
            (int)LSPErrorCodes::InvalidRequest, "The `Rename` LSP feature is experimental and disabled by default.");
        return LSPResult::make(move(gs), move(response));
    }

    prodCategoryCounterInc("lsp.messages.processed", "textDocument.prepareRename");

    auto result = setupLSPQueryByLoc(move(gs), params.textDocument->uri, *params.position,
                                     LSPMethod::TextDocumentCompletion, false);
    if (auto run1 = get_if<TypecheckRun>(&result)) {
        gs = move(run1->gs);
        // An explicit null indicates that we don't support this request (or that nothing was at the location).
        // Note: Need to correctly type variant here so it goes into right 'slot' of result variant.
        response->result = variant<JSONNullObject, unique_ptr<PrepareRenameResult>>(JSONNullObject());
        auto &queryResponses = run1->responses;
        if (!queryResponses.empty()) {
            auto resp = move(queryResponses[0]);
            // Only supports rename requests from constants and class definitions.
            if (auto constResp = resp->isConstant()) {
                response->result = getPrepareRenameResult(*gs, constResp->symbol);
            } else if (auto defResp = resp->isDefinition()) {
                if (defResp->symbol.data(*gs)->isClass()) {
                    response->result = getPrepareRenameResult(*gs, defResp->symbol);
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
