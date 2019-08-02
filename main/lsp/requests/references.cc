#include "absl/strings/match.h"
#include "core/lsp/QueryResponse.h"
#include "main/lsp/lsp.h"

using namespace std;

namespace sorbet::realmain::lsp {

LSPLocResult LSPLoop::getReferencesToSymbol(unique_ptr<core::GlobalState> gs, core::SymbolRef symbol) {
    vector<core::Loc> locs;
    if (symbol.exists()) {
        auto run2 = setupLSPQueryBySymbol(move(gs), symbol);
        gs = move(run2.gs);
        extractLocs(*gs, run2.responses, locs);
    }
    return LSPLocResult{move(gs), locs};
}

LSPResult LSPLoop::handleTextDocumentReferences(unique_ptr<core::GlobalState> gs, const MessageId &id,
                                                const ReferenceParams &params) {
    auto response = make_unique<ResponseMessage>("2.0", id, LSPMethod::TextDocumentReferences);
    ShowOperation op(*this, "References", "Finding all references...");
    prodCategoryCounterInc("lsp.messages.processed", "textDocument.references");

    auto result = setupLSPQueryByLoc(move(gs), params.textDocument->uri, *params.position,
                                     LSPMethod::TextDocumentCompletion, false);
    if (auto run1 = get_if<TypecheckRun>(&result)) {
        gs = move(run1->gs);
        // An explicit null indicates that we don't support this request (or that nothing was at the location).
        // Note: Need to correctly type variant here so it goes into right 'slot' of result variant.
        response->result = variant<JSONNullObject, vector<unique_ptr<Location>>>(JSONNullObject());
        auto &queryResponses = run1->responses;
        if (!queryResponses.empty()) {
            const bool fileIsTyped =
                uri2FileRef(params.textDocument->uri).data(*gs).strictLevel >= core::StrictLevel::True;
            auto resp = move(queryResponses[0]);
            // N.B.: Ignores literals.
            // If file is untyped, only supports find reference requests from constants and class definitions.
            if (auto constResp = resp->isConstant()) {
                auto result = getReferencesToSymbol(move(gs), constResp->symbol);
                gs = move(result.gs);
                response->result = locs2Locations(*gs, result.locs);
            } else if (auto defResp = resp->isDefinition()) {
                if (fileIsTyped || defResp->symbol.data(*gs)->isClass()) {
                    auto result = getReferencesToSymbol(move(gs), defResp->symbol);
                    gs = move(result.gs);
                    response->result = locs2Locations(*gs, result.locs);
                }
            } else if (fileIsTyped && resp->isIdent()) {
                auto identResp = resp->isIdent();
                auto loc = identResp->owner.data(*gs)->loc();
                if (loc.exists()) {
                    auto run2 =
                        runLSPQuery(move(gs), core::lsp::Query::createVarQuery(identResp->owner, identResp->variable),
                                    {loc.file()});
                    gs = move(run2.gs);
                    response->result = locs2Locations(*gs, extractLocs(*gs, run2.responses));
                }
            } else if (fileIsTyped && resp->isSend()) {
                auto sendResp = resp->isSend();
                auto start = sendResp->dispatchResult.get();
                vector<unique_ptr<Location>> locations;
                while (start != nullptr) {
                    if (start->main.method.exists() && !start->main.receiver->isUntyped()) {
                        auto result = getReferencesToSymbol(move(gs), start->main.method);
                        gs = move(result.gs);
                        locations = locs2Locations(*gs, result.locs, move(locations));
                    }
                    start = start->secondary.get();
                }
                response->result = move(locations);
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
