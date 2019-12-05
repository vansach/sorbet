#include "gtest/gtest.h"
// ^ Violates linting rules, so include first.
#include "ProtocolTest.h"
#include "absl/strings/match.h"
#include "common/common.h"
#include "test/helpers/lsp.h"

namespace sorbet::test::lsp {
using namespace std;
using namespace sorbet::realmain::lsp;

optional<SorbetTypecheckRunStatus> getTypecheckRunStatus(const LSPMessage &msg) {
    if (msg.isNotification() && msg.method() == LSPMethod::SorbetTypecheckRunInfo) {
        auto &typecheckInfo = get<unique_ptr<SorbetTypecheckRunInfo>>(msg.asNotification().params);
        return typecheckInfo->status;
    }
    return nullopt;
}

TEST_P(ProtocolTest, MultithreadedWrapperWorks) {
    assertDiagnostics(initializeLSP(), {});
    vector<unique_ptr<LSPMessage>> requests;
    requests.push_back(make_unique<LSPMessage>(make_unique<NotificationMessage>("2.0", LSPMethod::PAUSE, nullopt)));
    requests.push_back(
        changeFile("yolo1.rb", "# typed: true\nclass Foo2\n  def branch\n    2 + \"dog\"\n  end\nend\n", 2));
    requests.push_back(
        changeFile("yolo1.rb", "# typed: true\nclass Foo1\n  def branch\n    1 + \"bear\"\n  end\nend\n", 3));
    requests.push_back(make_unique<LSPMessage>(make_unique<NotificationMessage>("2.0", LSPMethod::RESUME, nullopt)));
    assertDiagnostics(send(move(requests)), {{"yolo1.rb", 3, "bear"}});
}

TEST_P(ProtocolTest, CancelsSlowPathWhenNewEditWouldTakeFastPathWithOldEdits) {
    auto initOptions = make_unique<SorbetInitializationOptions>();
    initOptions->enableTypecheckInfo = true;
    assertDiagnostics(initializeLSP(true /* supportsMarkdown */, move(initOptions)), {});

    // Create three files.
    assertDiagnostics(send(*openFile("foo.rb", "# typed: true\n\nclass Foo\n\nend\n")), {});
    assertDiagnostics(
        send(*openFile(
            "bar.rb",
            "# typed: true\n\nclass Bar\nextend T::Sig\n\nsig{returns(String)}\ndef hello\n\"hi\"\nend\nend\n")),
        {});
    // baz calls the method defined in bar
    assertDiagnostics(send(*openFile("baz.rb", "# typed: true\n\nclass Baz\nextend "
                                               "T::Sig\n\nsig{returns(String)}\ndef hello\nBar.new.hello\nend\nend\n")),
                      {});

    // Slow path edits two files. One introduces error.
    sendAsync(LSPMessage(make_unique<NotificationMessage>("2.0", LSPMethod::PAUSE, nullopt)));
    // Syntax error in foo.rb.
    sendAsync(*changeFile("foo.rb", "# typed: true\n\nclass Foo\ndef noend\nend\n", 2,
                          SorbetCancellationExpected::AfterResolver));
    // Typechecking error in bar.rb
    sendAsync(*changeFile(
        "bar.rb", "# typed: true\n\nclass Bar\nextend T::Sig\n\nsig{returns(Integer)}\ndef hello\n\"hi\"\nend\nend\n",
        2, SorbetCancellationExpected::AfterResolver));
    sendAsync(LSPMessage(make_unique<NotificationMessage>("2.0", LSPMethod::RESUME, nullopt)));
    // Wait for typechecking to begin to avoid races.
    {
        auto status = getTypecheckRunStatus(*readAsync());
        ASSERT_TRUE(status.has_value());
        ASSERT_EQ(*status, SorbetTypecheckRunStatus::Started);
    }

    // Make another edit that fixes syntax error and should take fast path.
    sendAsync(*changeFile("foo.rb", "# typed: true\n\nclass Foo\nend\n", 2));

    // Wait for first typecheck run to get canceled.
    {
        auto status = getTypecheckRunStatus(*readAsync());
        ASSERT_TRUE(status.has_value());
        ASSERT_EQ(*status, SorbetTypecheckRunStatus::Cancelled);
    }

    // Send a no-op to clear out the pipeline. Should have errors in bar and baz, but not foo.
    assertDiagnostics(send(LSPMessage(make_unique<NotificationMessage>("2.0", LSPMethod::SorbetFence, 20))),
                      {
                          {"bar.rb", 7, "Returning value that does not conform to method result type"},
                          {"baz.rb", 7, "Returning value that does not conform to method result type"},
                      });
}

// Same test as before, but this cancellation happens before the resolver pass. A bug could cause this edit to try to
// preempt.
TEST_P(ProtocolTest, CancelsSlowPathWhenNewEditWouldTakeFastPathWithOldEditsBeforeResolver) {
    auto initOptions = make_unique<SorbetInitializationOptions>();
    initOptions->enableTypecheckInfo = true;
    assertDiagnostics(initializeLSP(true /* supportsMarkdown */, move(initOptions)), {});

    // Create three files.
    assertDiagnostics(send(*openFile("foo.rb", "# typed: true\n\nclass Foo\n\nend\n")), {});
    assertDiagnostics(
        send(*openFile(
            "bar.rb",
            "# typed: true\n\nclass Bar\nextend T::Sig\n\nsig{returns(String)}\ndef hello\n\"hi\"\nend\nend\n")),
        {});
    // baz calls the method defined in bar
    assertDiagnostics(send(*openFile("baz.rb", "# typed: true\n\nclass Baz\nextend "
                                               "T::Sig\n\nsig{returns(String)}\ndef hello\nBar.new.hello\nend\nend\n")),
                      {});

    // Slow path edits two files. One introduces error.
    sendAsync(LSPMessage(make_unique<NotificationMessage>("2.0", LSPMethod::PAUSE, nullopt)));
    // Syntax error in foo.rb.
    sendAsync(*changeFile("foo.rb", "# typed: true\n\nclass Foo\ndef noend\nend\n", 2,
                          SorbetCancellationExpected::BeforeResolver));
    // Typechecking error in bar.rb
    sendAsync(*changeFile(
        "bar.rb", "# typed: true\n\nclass Bar\nextend T::Sig\n\nsig{returns(Integer)}\ndef hello\n\"hi\"\nend\nend\n",
        2, SorbetCancellationExpected::BeforeResolver));
    sendAsync(LSPMessage(make_unique<NotificationMessage>("2.0", LSPMethod::RESUME, nullopt)));
    // Wait for typechecking to begin to avoid races.
    {
        auto status = getTypecheckRunStatus(*readAsync());
        ASSERT_TRUE(status.has_value());
        ASSERT_EQ(*status, SorbetTypecheckRunStatus::Started);
    }

    // Make another edit that fixes syntax error and should take fast path.
    sendAsync(*changeFile("foo.rb", "# typed: true\n\nclass Foo\nend\n", 2));

    // Wait for first typecheck run to get canceled.
    {
        auto status = getTypecheckRunStatus(*readAsync());
        ASSERT_TRUE(status.has_value());
        ASSERT_EQ(*status, SorbetTypecheckRunStatus::Cancelled);
    }

    // Send a no-op to clear out the pipeline. Should have errors in bar and baz, but not foo.
    assertDiagnostics(send(LSPMessage(make_unique<NotificationMessage>("2.0", LSPMethod::SorbetFence, 20))),
                      {
                          {"bar.rb", 7, "Returning value that does not conform to method result type"},
                          {"baz.rb", 7, "Returning value that does not conform to method result type"},
                      });
}

TEST_P(ProtocolTest, CancelsSlowPathWhenNewEditWouldTakeSlowPath) {
    auto initOptions = make_unique<SorbetInitializationOptions>();
    initOptions->enableTypecheckInfo = true;
    assertDiagnostics(initializeLSP(true /* supportsMarkdown */, move(initOptions)), {});

    // Initial state: Two empty files.
    assertDiagnostics(send(*openFile("foo.rb", "")), {});
    assertDiagnostics(send(*openFile("bar.rb", "")), {});

    // Slow path 1: Edit foo to have an error since Bar doesn't exist. Expect a cancelation.
    sendAsync(*changeFile(
        "foo.rb", "# typed: true\n\nclass Foo\nextend T::Sig\nsig{returns(Integer)}\ndef foo\nBar.new.bar\nend\nend\n",
        2, SorbetCancellationExpected::AfterResolver));

    // Wait for typechecking to begin to avoid races.
    {
        auto status = getTypecheckRunStatus(*readAsync());
        ASSERT_TRUE(status.has_value());
        ASSERT_EQ(*status, SorbetTypecheckRunStatus::Started);
    }

    // Slow path 2: Bar defines the expected method, but declared with a non-integer return value (so foo now has a new
    // error).
    sendAsync(*changeFile(
        "bar.rb", "# typed: true\n\nclass Bar\nextend T::Sig\nsig{returns(String)}\ndef bar\n10\nend\nend\n", 2));

    // Wait for first typecheck run to get canceled.
    {
        auto status = getTypecheckRunStatus(*readAsync());
        ASSERT_TRUE(status.has_value());
        ASSERT_EQ(*status, SorbetTypecheckRunStatus::Cancelled);
    }

    // Send a no-op to clear out the pipeline. Should have one error per file.
    assertDiagnostics(send(LSPMessage(make_unique<NotificationMessage>("2.0", LSPMethod::SorbetFence, 20))),
                      {
                          {"foo.rb", 6, "Returning value that does not conform to method result type"},
                          {"bar.rb", 6, "Returning value that does not conform to method result type"},
                      });
}

TEST_P(ProtocolTest, CanPreemptSlowPathWithHover) {
    auto initOptions = make_unique<SorbetInitializationOptions>();
    initOptions->enableTypecheckInfo = true;
    assertDiagnostics(initializeLSP(true /* supportsMarkdown */, move(initOptions)), {});

    // Create a new file.
    assertDiagnostics(send(*openFile("foo.rb", "")), {});

    // Slow path: Edit foo to have a class with a documentation string.
    sendAsync(*changeFile("foo.rb", "# typed: true\n# A class that does things.\nclass Foo\nextend T::Sig\nend\n", 2,
                          SorbetCancellationExpected::None, 1));

    // Wait for typechecking to begin to avoid races.
    {
        auto status = getTypecheckRunStatus(*readAsync());
        ASSERT_TRUE(status.has_value());
        ASSERT_EQ(*status, SorbetTypecheckRunStatus::Started);
    }

    // Send a hover to request the documentation string.
    sendAsync(*hover("foo.rb", 2, 6));

    // First response should be hover.
    {
        auto response = readAsync();
        ASSERT_TRUE(response->isResponse());
        auto &hoverText =
            get<unique_ptr<Hover>>(get<variant<JSONNullObject, unique_ptr<Hover>>>(*response->asResponse().result));
        EXPECT_TRUE(absl::StrContains(hoverText->contents->value, "A class that does things"));
    }

    // Second should be typecheck run signaling that typechecking completed. No typechecking errors are expected.
    {
        auto status = getTypecheckRunStatus(*readAsync());
        ASSERT_TRUE(status.has_value());
        ASSERT_EQ(*status, SorbetTypecheckRunStatus::Ended);
    }
}

TEST_P(ProtocolTest, CanPreemptSlowPathWithFastPath) {
    auto initOptions = make_unique<SorbetInitializationOptions>();
    initOptions->enableTypecheckInfo = true;
    assertDiagnostics(initializeLSP(true /* supportsMarkdown */, move(initOptions)), {});

    // Create two new files.
    assertDiagnostics(send(*openFile("foo.rb", "")), {});
    assertDiagnostics(send(*openFile("bar.rb", "")), {});

    // Slow path: Edit foo to have a class with two methods and two errors, and add an error to bar.
    sendAsync(LSPMessage(make_unique<NotificationMessage>("2.0", LSPMethod::PAUSE, nullopt)));
    sendAsync(*changeFile("foo.rb",
                          "# typed: true\nclass Foo\nextend T::Sig\nsig{returns(Integer)}\ndef "
                          "bar\nbaz\nend\nsig{returns(Float)}\ndef baz\n'not a float'\nend\nend\n",
                          2, SorbetCancellationExpected::None, 1));
    sendAsync(*changeFile(
        "bar.rb", "# typed: true\nclass Bar\nextend T::Sig\nsig{returns(String)}\ndef branch\n1\nend\nend\n", 3));
    sendAsync(LSPMessage(make_unique<NotificationMessage>("2.0", LSPMethod::RESUME, nullopt)));

    // Wait for typechecking to begin to avoid races.
    {
        auto status = getTypecheckRunStatus(*readAsync());
        ASSERT_TRUE(status.has_value());
        ASSERT_EQ(*status, SorbetTypecheckRunStatus::Started);
    }

    // Fast path 1: Correct _one_ error.
    sendAsync(*changeFile("foo.rb",
                          "# typed: true\nclass Foo\nextend T::Sig\nsig{returns(Integer)}\ndef "
                          "bar\n10\nend\nsig{returns(Float)}\ndef baz\n'not a float'\nend\nend\n",
                          3));

    // Send a no-op to clear out the pipeline. Should have two error now: bar.rb from slow path and foo.rb from fast
    // path.
    assertDiagnostics(send(LSPMessage(make_unique<NotificationMessage>("2.0", LSPMethod::SorbetFence, 20))),
                      {
                          {"foo.rb", 9, "Returning value that does not conform to method result type"},
                          {"bar.rb", 5, "Returning value that does not conform to method result type"},
                      });
}

TEST_P(ProtocolTest, CanPreemptSlowPathWithFastPathThatFixesAllErrors) {
    auto initOptions = make_unique<SorbetInitializationOptions>();
    initOptions->enableTypecheckInfo = true;
    assertDiagnostics(initializeLSP(true /* supportsMarkdown */, move(initOptions)), {});

    // Create two new files.
    assertDiagnostics(send(*openFile("foo.rb", "")), {});
    assertDiagnostics(send(*openFile("bar.rb", "")), {});

    // Slow path: Edit foo to have a class with an error that also causes an error in bar
    sendAsync(LSPMessage(make_unique<NotificationMessage>("2.0", LSPMethod::PAUSE, nullopt)));
    sendAsync(*changeFile("foo.rb",
                          "# typed: true\nclass Foo\nextend T::Sig\nsig{returns(Integer)}\ndef "
                          "bar\n'hello'\nend\nend\n",
                          2, SorbetCancellationExpected::None, 1));
    sendAsync(*changeFile(
        "bar.rb", "# typed: true\nclass Bar\nextend T::Sig\nsig{returns(String)}\ndef str\nFoo.new.bar\nend\nend\n",
        3));
    sendAsync(LSPMessage(make_unique<NotificationMessage>("2.0", LSPMethod::RESUME, nullopt)));

    // Wait for typechecking to begin to avoid races.
    {
        auto status = getTypecheckRunStatus(*readAsync());
        ASSERT_TRUE(status.has_value());
        ASSERT_EQ(*status, SorbetTypecheckRunStatus::Started);
    }

    // Fast path 1: Correct return type on foo::bar, which should fix foo.rb and bar.rb.
    sendAsync(*changeFile("foo.rb",
                          "# typed: true\nclass Foo\nextend T::Sig\nsig{returns(String)}\ndef "
                          "bar\n'hello'\nend\nend\n",
                          3));

    // Send a no-op to clear out the pipeline. Should have no errors at end of both typechecking runs.
    assertDiagnostics(send(LSPMessage(make_unique<NotificationMessage>("2.0", LSPMethod::SorbetFence, 20))), {});
}

TEST_P(ProtocolTest, CanPreemptSlowPathWithFastPathAndThenCancelBoth) {
    auto initOptions = make_unique<SorbetInitializationOptions>();
    initOptions->enableTypecheckInfo = true;
    assertDiagnostics(initializeLSP(true /* supportsMarkdown */, move(initOptions)), {});

    // Create three new files! foo.rb defines a class, bar.rb defines a class and method used in baz.rb.
    assertDiagnostics(send(*openFile("foo.rb", "# typed: true\nclass Foo\nextend T::Sig\nend")), {});
    assertDiagnostics(
        send(*openFile("bar.rb",
                       "# typed: true\nclass Bar\nextend T::Sig\nsig{returns(String)}\ndef str\n'hi'\nend\nend\n")),
        {});
    assertDiagnostics(
        send(*openFile(
            "baz.rb",
            "# typed: true\nclass Baz\nextend T::Sig\nsig{returns(String)}\ndef bar\nBar.new.str\nend\nend\n")),
        {});

    // Slow path: foo.rb will have a syntax error
    sendAsync(*changeFile("foo.rb", "# typed: true\nclass Foo\nextend T::Sig\n", 2,
                          SorbetCancellationExpected::AfterResolver, 1));

    // Wait for typechecking to begin to avoid races.
    {
        auto status = getTypecheckRunStatus(*readAsync());
        ASSERT_TRUE(status.has_value());
        ASSERT_EQ(*status, SorbetTypecheckRunStatus::Started);
    }

    // Fast path [preempt]: Change return type in bar.rb, which indirectly causes an error in baz.rb too.
    sendAsync(*changeFile(
        "bar.rb", "# typed: true\nclass Bar\nextend T::Sig\nsig{returns(Integer)}\ndef str\n'hi'\nend\nend\n", 4));

    // Wait for typechecking to begin to avoid races.
    {
        auto status = getTypecheckRunStatus(*readAsync());
        ASSERT_TRUE(status.has_value());
        ASSERT_EQ(*status, SorbetTypecheckRunStatus::Started);
    }

    // Fast path [cancel]: Fix syntax error. Foo should not have any errors.
    assertDiagnostics(send(*changeFile("foo.rb", "# typed: true\nclass Foo\nextend T::Sig\nend", 4)),
                      {
                          {"bar.rb", 5, "Returning value that does not conform to method result type"},
                          {"baz.rb", 5, "Returning value that does not conform to method result type"},
                      });
}

/*TEST_P(ProtocolTest, CanPreemptInitializationTypecheking) {
    // TODO: Need to populate disk with file then send watchman...?

    // Send 'initialize' message.
    sendAsync(LSPMessage(make_unique<RequestMessage>(
        "2.0", nextId++, LSPMethod::Initialize,
        makeInitializeParams(string(rootPath), string(rootUri), true, nullopt))));

    // "initialize" response
    {
        auto msg = readAsync();
        ASSERT_TRUE(msg->isResponse());
    }

    // Send an initialized method to complete handshake, indicating that we expect 2 preemptions.
    {
        auto initializedParams = make_unique<InitializedParams>();
        initializedParams->sorbetPreemptionsExpected = 2;
        sendAsync(LSPMessage(make_unique<NotificationMessage>("2.0", LSPMethod::Initialized, move(initializedParams))));
    }

    //
}*/

// Run these tests in multi-threaded mode.
INSTANTIATE_TEST_SUITE_P(MultithreadedProtocolTests, ProtocolTest, testing::Values(true));

} // namespace sorbet::test::lsp
