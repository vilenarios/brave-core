/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <array>

#include "base/check_op.h"
#include "base/containers/span.h"
#include "base/feature_list.h"
#include "base/memory/raw_ptr.h"
#include "base/path_service.h"
#include "base/types/cxx23_to_underlying.h"
#include "base/types/zip.h"
#include "brave/browser/ephemeral_storage/ephemeral_storage_service_factory.h"
#include "brave/components/brave_shields/content/browser/brave_shields_util.h"
#include "brave/components/constants/brave_paths.h"
#include "brave/components/ephemeral_storage/ephemeral_storage_service.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/content_settings/core/browser/cookie_settings.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_mock_cert_verifier.h"
#include "content/public/test/test_navigation_observer.h"
#include "net/base/features.h"
#include "net/dns/mock_host_resolver.h"

namespace {

// Helper for waiting for a change of the active tab.
// Users can wait for the change via WaitForActiveTabChange method.
// DCHECKs ensure that only one change happens during the lifetime of a
// TabActivationWaiter instance.
class TabActivationWaiter : public TabStripModelObserver {
 public:
  explicit TabActivationWaiter(TabStripModel* tab_strip_model) {
    tab_strip_model->AddObserver(this);
  }

  TabActivationWaiter(const TabActivationWaiter&) = delete;
  TabActivationWaiter& operator=(const TabActivationWaiter&) = delete;

  void WaitForActiveTabChange() {
    if (number_of_unconsumed_active_tab_changes_ == 0) {
      // Wait until TabStripModelObserver::ActiveTabChanged will get called.
      message_loop_runner_ = new content::MessageLoopRunner;
      message_loop_runner_->Run();
    }

    // "consume" one tab activation event.
    DCHECK_EQ(1, number_of_unconsumed_active_tab_changes_);
    number_of_unconsumed_active_tab_changes_--;
  }

  // TabStripModelObserver overrides.
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override {
    if (tab_strip_model->empty() || !selection.active_tab_changed())
      return;

    number_of_unconsumed_active_tab_changes_++;
    DCHECK_EQ(1, number_of_unconsumed_active_tab_changes_);
    if (message_loop_runner_)
      message_loop_runner_->Quit();
  }

 private:
  scoped_refptr<content::MessageLoopRunner> message_loop_runner_;
  int number_of_unconsumed_active_tab_changes_ = 0;
};

constexpr char kEphemeralStorageTestPage[] = "/storage/ephemeral-storage.html";

enum class StorageResult {
  kSuccess = 0,
  kEmpty = 1,
  kBlocked = 2,
  kNA = 6,
};

using ResultSet = std::array<StorageResult, 4u>;

enum class StorageType {
  kCookies,
  kLocalStorage,
  kSessionStorage,
  kIndexDB,
};

// Converts a StorageType to its corresponding representation in the report
// from the page's JS code.
std::string AsString(StorageType t) {
  switch (t) {
    case StorageType::kCookies:
      return "cookies";
    case StorageType::kLocalStorage:
      return "local-storage";
    case StorageType::kSessionStorage:
      return "session-storage";
    case StorageType::kIndexDB:
      return "index-db";
  }
}

}  // namespace

// This test suite recreates the behavior of the ephemeral storage tests
// available on Brave's QA test pages, whose source is located at
// https://github.com/brave-experiments/qa-test-pages
//
// The tests check four types of storage across four different storage
// contexts. As such, each test expects a 4x4 matrix of storage reading
// results.
//
// The rows of the matrix are as follows:
// - cookies
// - local storage
// - session storage
// - index DB
//
// The columns of the matrix are as follows:
// - this frame
// - local frame
// - remote frame
// - nested frame
class EphemeralStorageQaBrowserTest : public InProcessBrowserTest {
 public:
  EphemeralStorageQaBrowserTest()
      : https_server_(net::EmbeddedTestServer::TYPE_HTTPS) {
    std::vector<base::test::FeatureRef> disabled_features;
    feature_list_.InitWithFeatures({net::features::kBraveEphemeralStorage},
                                   disabled_features);
  }

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    base::FilePath test_data_dir;
    base::PathService::Get(brave::DIR_TEST_DATA, &test_data_dir);
    embedded_test_server()->ServeFilesFromDirectory(
        test_data_dir.Append(FILE_PATH_LITERAL("ephemeral-storage")));
    content::SetupCrossSiteRedirector(embedded_test_server());
    ASSERT_TRUE(embedded_test_server()->Start());
    mock_cert_verifier_.mock_cert_verifier()->set_default_result(net::OK);
    host_resolver()->AddRule("*", "127.0.0.1");
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
    mock_cert_verifier_.SetUpCommandLine(command_line);
  }

  void SetUpInProcessBrowserTestFixture() override {
    InProcessBrowserTest::SetUpInProcessBrowserTestFixture();
    mock_cert_verifier_.SetUpInProcessBrowserTestFixture();
  }

  void TearDownInProcessBrowserTestFixture() override {
    mock_cert_verifier_.TearDownInProcessBrowserTestFixture();
    InProcessBrowserTest::TearDownInProcessBrowserTestFixture();
  }

  HostContentSettingsMap* content_settings() {
    return HostContentSettingsMapFactory::GetForProfile(browser()->profile());
  }

  net::EmbeddedTestServer* embedded_test_server() { return &https_server_; }

  void SetThirdPartyCookiePref(bool setting) {
    browser()->profile()->GetPrefs()->SetInteger(
        prefs::kCookieControlsMode,
        base::to_underlying(
            setting ? content_settings::CookieControlsMode::kBlockThirdParty
                    : content_settings::CookieControlsMode::kOff));
  }

  void SetCookiePref(ContentSetting setting) {
    browser()->profile()->GetPrefs()->SetInteger(
        "profile.default_content_setting_values.cookies", setting);
  }

  void SetCookieControlType(brave_shields::ControlType control_type) {
    brave_shields::SetCookieControlType(
        content_settings(), browser()->profile()->GetPrefs(), control_type,
        embedded_test_server()->GetURL("dev-pages.brave.software",
                                       kEphemeralStorageTestPage));
  }

  // Starts the JS test code on the QA page to populate storage values so that
  // they can be read back later in other contexts.
  void ClickStartTest(content::WebContents* contents) {
    content::DOMMessageQueue queue(contents);
    ExecuteScriptAsync(contents, "window.setStorageAction()");
    std::string message;
    while (message != "\"button operation completed\"") {
      ASSERT_TRUE(queue.WaitForMessage(&message));
    }
  }

  // Prepares the test page for validation of test results by reading storage
  // and writing to the 2D results matrix.
  void ClickReadValues(content::WebContents* contents) {
    content::DOMMessageQueue queue(contents);
    ExecuteScriptAsync(contents, "window.readValuesAction()");
    std::string message;
    while (message != "\"button operation completed\"") {
      ASSERT_TRUE(queue.WaitForMessage(&message));
    }
  }

  // Performs a navigation in the current session to the other-origin page by
  // clicking the link with the corresponding href attribute.
  void NavigateOtherOrigin(content::WebContents* contents) {
    {
      TabActivationWaiter tab_activation_waiter(browser()->tab_strip_model());
      ExecuteScriptAsync(
          contents,
          "document.querySelector('.other-origin.ephem-storage-test').click()");
      tab_activation_waiter.WaitForActiveTabChange();
    }
    content::TestNavigationObserver navigation_observer(
        browser()->tab_strip_model()->GetActiveWebContents());
    navigation_observer.Wait();
    ASSERT_TRUE(navigation_observer.last_navigation_succeeded());
  }

  // Performs a navigation in the current session to the same-origin page by
  // clicking the link with the corresponding href attribute.
  void NavigateSameOrigin(content::WebContents* contents) {
    {
      TabActivationWaiter tab_activation_waiter(browser()->tab_strip_model());
      ExecuteScriptAsync(
          contents,
          "document.querySelector('.this-origin.ephem-storage-test').click()");
      tab_activation_waiter.WaitForActiveTabChange();
    }
    content::TestNavigationObserver navigation_observer(
        browser()->tab_strip_model()->GetActiveWebContents());
    navigation_observer.Wait();
    ASSERT_TRUE(navigation_observer.last_navigation_succeeded());
  }

  // Checks that the test page's generated storage report matches the expected
  // values
  void CheckStorageResults(content::WebContents* contents,
                           base::span<const ResultSet, 4u> expected) {
    static constexpr auto kStringTypes =
        std::to_array({StorageType::kCookies, StorageType::kLocalStorage,
                       StorageType::kSessionStorage, StorageType::kIndexDB});

    for (auto [type, results] : base::zip(kStringTypes, expected)) {
      CheckStorageResults(contents, type, results);
    }
  }

  // Checks a particular row of the 2D storage results matrix, corresponding to
  // a single storage type
  void CheckStorageResults(content::WebContents* contents,
                           StorageType storage_type,
                           const ResultSet& expected) {
    SCOPED_TRACE(::testing::Message()
                 << "StorageType: " << base::to_underlying(storage_type));

    // The frames we want to test against and in the expected order.
    static constexpr auto kFrames = std::to_array<std::string_view>(
        {"this-frame", "local-frame", "remote-frame", "nested-frame"});

    for (auto [result, frame] : base::zip(expected, kFrames)) {
      EXPECT_EQ(
          base::to_underlying(result),
          content::EvalJs(contents, content::JsReplace(
                                        "window.generateStorageReport().then("
                                        "report => report[$1][$2])",
                                        AsString(storage_type), frame)));
    }
  }

  // Tests storage stored and then loaded within a single page session.
  void TestInitialCase(base::span<const ResultSet, 4u> expected) {
    ASSERT_TRUE(original_tab_);

    CheckStorageResults(original_tab_, expected);
  }

  // Tests storage stored from one page and then loaded from a remote page in
  // the same browsing session.
  void TestRemotePageSameSession(base::span<const ResultSet, 4u> expected) {
    ASSERT_TRUE(original_tab_);
    ASSERT_EQ(1, tabs_->count());

    NavigateOtherOrigin(original_tab_);
    ASSERT_EQ(2, tabs_->count());
    ASSERT_EQ(1, tabs_->active_index());

    content::WebContents* contents = tabs_->GetActiveWebContents();

    ClickReadValues(contents);

    CheckStorageResults(contents, expected);
  }

  // Tests storage stored from one page and then loaded from a remote page in a
  // new browsing session.
  void TestRemotePageNewSession(base::span<const ResultSet, 4u> expected) {
    ASSERT_TRUE(original_tab_);
    ASSERT_EQ(1, tabs_->count());

    chrome::NewTab(browser());
    ASSERT_EQ(2, tabs_->count());
    ASSERT_EQ(1, tabs_->active_index());

    std::string target =
        content::EvalJs(
            original_tab_.get(),
            "document.getElementById('continue-test-url-step-3').value")
            .ExtractString();
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL(target)));

    content::WebContents* contents = tabs_->GetActiveWebContents();

    ClickReadValues(contents);

    CheckStorageResults(contents, expected);
  }

  // Tests storage stored from one page and then loaded from the same page in a
  // new tab from the same browsing session.
  void TestThisPageSameSession(base::span<const ResultSet, 4u> expected) {
    ASSERT_TRUE(original_tab_);
    ASSERT_EQ(1, tabs_->count());

    NavigateSameOrigin(original_tab_);
    ASSERT_EQ(2, tabs_->count());
    ASSERT_EQ(1, tabs_->active_index());

    content::WebContents* contents = tabs_->GetActiveWebContents();

    ClickReadValues(contents);

    CheckStorageResults(contents, expected);
  }

  // Tests storage stored from one page and then loaded from the same page in a
  // new tab from a different browsing session.
  void TestThisPageDifferentSession(base::span<const ResultSet, 4u> expected) {
    ASSERT_TRUE(original_tab_);
    ASSERT_EQ(1, tabs_->count());

    chrome::NewTab(browser());
    ASSERT_EQ(2, tabs_->count());
    ASSERT_EQ(1, tabs_->active_index());

    std::string target =
        content::EvalJs(
            original_tab_.get(),
            "document.getElementById('continue-test-url-step-5').value")
            .ExtractString();
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL(target)));

    content::WebContents* contents = tabs_->GetActiveWebContents();

    ClickReadValues(contents);

    CheckStorageResults(contents, expected);
  }

  // Tests storage stored from one page and then loaded from the same page
  // after having reset the browsing session.
  void TestNewPageResetSession(base::span<const ResultSet, 4u> expected) {
    ASSERT_TRUE(original_tab_);
    ASSERT_EQ(1, tabs_->count());

    chrome::NewTab(browser());
    ASSERT_EQ(2, tabs_->count());
    ASSERT_EQ(1, tabs_->active_index());

    std::string target =
        content::EvalJs(
            original_tab_.get(),
            "document.getElementById('continue-test-url-step-6').value")
            .ExtractString();

    const int previous_tab_count = browser()->tab_strip_model()->count();
    tabs_->CloseWebContentsAt(tabs_->GetIndexOfWebContents(original_tab_),
                              TabCloseTypes::CLOSE_NONE);
    ASSERT_EQ(previous_tab_count - 1, browser()->tab_strip_model()->count());

    EphemeralStorageServiceFactory::GetInstance()
        ->GetForContext(browser()->profile())
        ->FireCleanupTimersForTesting();

    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL(target)));

    content::WebContents* contents = tabs_->GetActiveWebContents();

    ClickReadValues(contents);

    CheckStorageResults(contents, expected);
  }

  void SetupTestPage() {
    tabs_ = browser()->tab_strip_model();

    GURL tab_url = embedded_test_server()->GetURL("dev-pages.brave.software",
                                                  kEphemeralStorageTestPage);
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), tab_url));
    original_tab_ = tabs_->GetActiveWebContents();

    ClickStartTest(original_tab_);
  }

 protected:
  content::ContentMockCertVerifier mock_cert_verifier_;
  net::test_server::EmbeddedTestServer https_server_;
  base::test::ScopedFeatureList feature_list_;

 private:
  raw_ptr<content::WebContents, DanglingUntriaged> original_tab_ = nullptr;
  raw_ptr<TabStripModel, DanglingUntriaged> tabs_ = nullptr;
};

IN_PROC_BROWSER_TEST_F(EphemeralStorageQaBrowserTest,
                       CrossSiteCookiesBlockedInitial) {
  SetCookiePref(CONTENT_SETTING_ALLOW);
  SetThirdPartyCookiePref(true);

  SetupTestPage();

  const ResultSet expected[4] = {
      {StorageResult::kSuccess, StorageResult::kSuccess,
       StorageResult::kSuccess, StorageResult::kSuccess},
      {StorageResult::kSuccess, StorageResult::kSuccess,
       StorageResult::kSuccess, StorageResult::kEmpty},
      {StorageResult::kSuccess, StorageResult::kSuccess,
       StorageResult::kSuccess, StorageResult::kEmpty},
      {StorageResult::kSuccess, StorageResult::kSuccess,
       StorageResult::kSuccess, StorageResult::kEmpty},
  };
  TestInitialCase(expected);
}

IN_PROC_BROWSER_TEST_F(EphemeralStorageQaBrowserTest,
                       CrossSiteCookiesBlockedRemotePageSameSession) {
  SetCookiePref(CONTENT_SETTING_ALLOW);
  SetThirdPartyCookiePref(true);

  SetupTestPage();

  const ResultSet expected[4] = {
      {StorageResult::kEmpty, StorageResult::kEmpty, StorageResult::kEmpty,
       StorageResult::kNA},
      {StorageResult::kEmpty, StorageResult::kEmpty, StorageResult::kEmpty,
       StorageResult::kNA},
      {StorageResult::kEmpty, StorageResult::kEmpty, StorageResult::kEmpty,
       StorageResult::kNA},
      {StorageResult::kEmpty, StorageResult::kEmpty, StorageResult::kEmpty,
       StorageResult::kNA},
  };
  TestRemotePageSameSession(expected);
}

IN_PROC_BROWSER_TEST_F(EphemeralStorageQaBrowserTest,
                       CrossSiteCookiesBlockedRemotePageNewSession) {
  SetCookiePref(CONTENT_SETTING_ALLOW);
  SetThirdPartyCookiePref(true);

  SetupTestPage();

  const ResultSet expected[4] = {
      {StorageResult::kEmpty, StorageResult::kEmpty, StorageResult::kEmpty,
       StorageResult::kNA},
      {StorageResult::kEmpty, StorageResult::kEmpty, StorageResult::kEmpty,
       StorageResult::kNA},
      {StorageResult::kEmpty, StorageResult::kEmpty, StorageResult::kEmpty,
       StorageResult::kNA},
      {StorageResult::kEmpty, StorageResult::kEmpty, StorageResult::kEmpty,
       StorageResult::kNA},
  };
  TestRemotePageNewSession(expected);
}

IN_PROC_BROWSER_TEST_F(EphemeralStorageQaBrowserTest,
                       CrossSiteCookiesBlockedThisPageSameSession) {
  SetCookiePref(CONTENT_SETTING_ALLOW);
  SetThirdPartyCookiePref(true);

  SetupTestPage();

  const ResultSet expected[4] = {
      {StorageResult::kSuccess, StorageResult::kSuccess,
       StorageResult::kSuccess, StorageResult::kNA},
      {StorageResult::kSuccess, StorageResult::kSuccess,
       StorageResult::kSuccess, StorageResult::kNA},
      {StorageResult::kSuccess, StorageResult::kSuccess,
       StorageResult::kSuccess, StorageResult::kNA},
      {StorageResult::kSuccess, StorageResult::kSuccess,
       StorageResult::kSuccess, StorageResult::kNA},
  };
  TestThisPageSameSession(expected);
}

IN_PROC_BROWSER_TEST_F(EphemeralStorageQaBrowserTest,
                       CrossSiteCookiesBlockedThisPageDifferentSession) {
  SetCookiePref(CONTENT_SETTING_ALLOW);
  SetThirdPartyCookiePref(true);

  SetupTestPage();

  const ResultSet expected[4] = {
      {StorageResult::kSuccess, StorageResult::kSuccess,
       StorageResult::kSuccess, StorageResult::kNA},
      {StorageResult::kSuccess, StorageResult::kSuccess,
       StorageResult::kSuccess, StorageResult::kNA},
      {StorageResult::kEmpty, StorageResult::kEmpty, StorageResult::kEmpty,
       StorageResult::kNA},
      {StorageResult::kSuccess, StorageResult::kSuccess,
       StorageResult::kSuccess, StorageResult::kNA},
  };
  TestThisPageDifferentSession(expected);
}

IN_PROC_BROWSER_TEST_F(EphemeralStorageQaBrowserTest,
                       CrossSiteCookiesBlockedNewPageResetSession) {
  SetCookiePref(CONTENT_SETTING_ALLOW);
  SetThirdPartyCookiePref(true);

  SetupTestPage();

  const ResultSet expected[4] = {
      {StorageResult::kSuccess, StorageResult::kSuccess, StorageResult::kEmpty,
       StorageResult::kNA},
      {StorageResult::kSuccess, StorageResult::kSuccess, StorageResult::kEmpty,
       StorageResult::kNA},
      {StorageResult::kEmpty, StorageResult::kEmpty, StorageResult::kEmpty,
       StorageResult::kNA},
      {StorageResult::kSuccess, StorageResult::kSuccess, StorageResult::kEmpty,
       StorageResult::kNA},
  };
  TestNewPageResetSession(expected);
}

IN_PROC_BROWSER_TEST_F(EphemeralStorageQaBrowserTest, CookiesBlockedInitial) {
  SetCookiePref(CONTENT_SETTING_BLOCK);

  SetupTestPage();

  const ResultSet expected[4] = {
      {StorageResult::kBlocked, StorageResult::kBlocked,
       StorageResult::kBlocked, StorageResult::kBlocked},
      {StorageResult::kBlocked, StorageResult::kBlocked,
       StorageResult::kBlocked, StorageResult::kBlocked},
      {StorageResult::kBlocked, StorageResult::kBlocked,
       StorageResult::kBlocked, StorageResult::kBlocked},
      {StorageResult::kBlocked, StorageResult::kBlocked,
       StorageResult::kBlocked, StorageResult::kBlocked},
  };
  TestInitialCase(expected);
}

IN_PROC_BROWSER_TEST_F(EphemeralStorageQaBrowserTest,
                       CookiesBlockedRemotePageSameSession) {
  SetCookiePref(CONTENT_SETTING_BLOCK);

  SetupTestPage();

  const ResultSet expected[4] = {
      {StorageResult::kBlocked, StorageResult::kBlocked,
       StorageResult::kBlocked, StorageResult::kNA},
      {StorageResult::kBlocked, StorageResult::kBlocked,
       StorageResult::kBlocked, StorageResult::kNA},
      {StorageResult::kBlocked, StorageResult::kBlocked,
       StorageResult::kBlocked, StorageResult::kNA},
      {StorageResult::kBlocked, StorageResult::kBlocked,
       StorageResult::kBlocked, StorageResult::kNA},
  };
  TestRemotePageSameSession(expected);
}

IN_PROC_BROWSER_TEST_F(EphemeralStorageQaBrowserTest,
                       CookiesBlockedRemotePageNewSession) {
  SetCookiePref(CONTENT_SETTING_BLOCK);

  SetupTestPage();

  const ResultSet expected[4] = {
      {StorageResult::kBlocked, StorageResult::kBlocked,
       StorageResult::kBlocked, StorageResult::kNA},
      {StorageResult::kBlocked, StorageResult::kBlocked,
       StorageResult::kBlocked, StorageResult::kNA},
      {StorageResult::kBlocked, StorageResult::kBlocked,
       StorageResult::kBlocked, StorageResult::kNA},
      {StorageResult::kBlocked, StorageResult::kBlocked,
       StorageResult::kBlocked, StorageResult::kNA},
  };
  TestRemotePageNewSession(expected);
}

IN_PROC_BROWSER_TEST_F(EphemeralStorageQaBrowserTest,
                       CookiesBlockedThisPageSameSession) {
  SetCookiePref(CONTENT_SETTING_BLOCK);

  SetupTestPage();

  const ResultSet expected[4] = {
      {StorageResult::kBlocked, StorageResult::kBlocked,
       StorageResult::kBlocked, StorageResult::kNA},
      {StorageResult::kBlocked, StorageResult::kBlocked,
       StorageResult::kBlocked, StorageResult::kNA},
      {StorageResult::kBlocked, StorageResult::kBlocked,
       StorageResult::kBlocked, StorageResult::kNA},
      {StorageResult::kBlocked, StorageResult::kBlocked,
       StorageResult::kBlocked, StorageResult::kNA},
  };
  TestThisPageSameSession(expected);
}

IN_PROC_BROWSER_TEST_F(EphemeralStorageQaBrowserTest,
                       CookiesBlockedThisPageDifferentSession) {
  SetCookiePref(CONTENT_SETTING_BLOCK);

  SetupTestPage();

  const ResultSet expected[4] = {
      {StorageResult::kBlocked, StorageResult::kBlocked,
       StorageResult::kBlocked, StorageResult::kNA},
      {StorageResult::kBlocked, StorageResult::kBlocked,
       StorageResult::kBlocked, StorageResult::kNA},
      {StorageResult::kBlocked, StorageResult::kBlocked,
       StorageResult::kBlocked, StorageResult::kNA},
      {StorageResult::kBlocked, StorageResult::kBlocked,
       StorageResult::kBlocked, StorageResult::kNA},
  };
  TestThisPageDifferentSession(expected);
}

IN_PROC_BROWSER_TEST_F(EphemeralStorageQaBrowserTest,
                       CookiesBlockedNewPageResetSession) {
  SetCookiePref(CONTENT_SETTING_BLOCK);

  SetupTestPage();

  const ResultSet expected[4] = {
      {StorageResult::kBlocked, StorageResult::kBlocked,
       StorageResult::kBlocked, StorageResult::kNA},
      {StorageResult::kBlocked, StorageResult::kBlocked,
       StorageResult::kBlocked, StorageResult::kNA},
      {StorageResult::kBlocked, StorageResult::kBlocked,
       StorageResult::kBlocked, StorageResult::kNA},
      {StorageResult::kBlocked, StorageResult::kBlocked,
       StorageResult::kBlocked, StorageResult::kNA},
  };
  TestNewPageResetSession(expected);
}

IN_PROC_BROWSER_TEST_F(EphemeralStorageQaBrowserTest, CookiesAllowedInitial) {
  SetCookiePref(CONTENT_SETTING_ALLOW);
  SetThirdPartyCookiePref(false);

  SetupTestPage();

  const ResultSet expected[4] = {
      {StorageResult::kSuccess, StorageResult::kSuccess,
       StorageResult::kSuccess, StorageResult::kSuccess},
      {StorageResult::kSuccess, StorageResult::kSuccess,
       StorageResult::kSuccess, StorageResult::kEmpty},
      {StorageResult::kSuccess, StorageResult::kSuccess,
       StorageResult::kSuccess, StorageResult::kEmpty},
      {StorageResult::kSuccess, StorageResult::kSuccess,
       StorageResult::kSuccess, StorageResult::kEmpty},
  };
  TestInitialCase(expected);
}

IN_PROC_BROWSER_TEST_F(EphemeralStorageQaBrowserTest,
                       CookiesAllowedRemotePageSameSession) {
  SetCookiePref(CONTENT_SETTING_ALLOW);
  SetThirdPartyCookiePref(false);

  SetupTestPage();

  const ResultSet expected[4] = {
      {StorageResult::kSuccess, StorageResult::kSuccess,
       StorageResult::kSuccess, StorageResult::kNA},
      {StorageResult::kEmpty, StorageResult::kEmpty, StorageResult::kEmpty,
       StorageResult::kNA},
      {StorageResult::kEmpty, StorageResult::kEmpty, StorageResult::kEmpty,
       StorageResult::kNA},
      {StorageResult::kEmpty, StorageResult::kEmpty, StorageResult::kEmpty,
       StorageResult::kNA},
  };
  TestRemotePageSameSession(expected);
}

IN_PROC_BROWSER_TEST_F(EphemeralStorageQaBrowserTest,
                       CookiesAllowedRemotePageNewSession) {
  SetCookiePref(CONTENT_SETTING_ALLOW);
  SetThirdPartyCookiePref(false);

  SetupTestPage();

  const ResultSet expected[4] = {
      {StorageResult::kSuccess, StorageResult::kSuccess,
       StorageResult::kSuccess, StorageResult::kNA},
      {StorageResult::kEmpty, StorageResult::kEmpty, StorageResult::kEmpty,
       StorageResult::kNA},
      {StorageResult::kEmpty, StorageResult::kEmpty, StorageResult::kEmpty,
       StorageResult::kNA},
      {StorageResult::kEmpty, StorageResult::kEmpty, StorageResult::kEmpty,
       StorageResult::kNA},
  };
  TestRemotePageNewSession(expected);
}

IN_PROC_BROWSER_TEST_F(EphemeralStorageQaBrowserTest,
                       CookiesAllowedThisPageSameSession) {
  SetCookiePref(CONTENT_SETTING_ALLOW);
  SetThirdPartyCookiePref(false);

  SetupTestPage();

  const ResultSet expected[4] = {
      {StorageResult::kSuccess, StorageResult::kSuccess,
       StorageResult::kSuccess, StorageResult::kNA},
      {StorageResult::kSuccess, StorageResult::kSuccess,
       StorageResult::kSuccess, StorageResult::kNA},
      {StorageResult::kSuccess, StorageResult::kSuccess,
       StorageResult::kSuccess, StorageResult::kNA},
      {StorageResult::kSuccess, StorageResult::kSuccess,
       StorageResult::kSuccess, StorageResult::kNA},
  };
  TestThisPageSameSession(expected);
}

IN_PROC_BROWSER_TEST_F(EphemeralStorageQaBrowserTest,
                       CookiesAllowedThisPageDifferentSession) {
  SetCookiePref(CONTENT_SETTING_ALLOW);
  SetThirdPartyCookiePref(false);

  SetupTestPage();

  const ResultSet expected[4] = {
      {StorageResult::kSuccess, StorageResult::kSuccess,
       StorageResult::kSuccess, StorageResult::kNA},
      {StorageResult::kSuccess, StorageResult::kSuccess,
       StorageResult::kSuccess, StorageResult::kNA},
      {StorageResult::kEmpty, StorageResult::kEmpty, StorageResult::kEmpty,
       StorageResult::kNA},
      {StorageResult::kSuccess, StorageResult::kSuccess,
       StorageResult::kSuccess, StorageResult::kNA},
  };
  TestThisPageDifferentSession(expected);
}

IN_PROC_BROWSER_TEST_F(EphemeralStorageQaBrowserTest,
                       CookiesAllowedNewPageResetSession) {
  SetCookiePref(CONTENT_SETTING_ALLOW);
  SetThirdPartyCookiePref(false);

  SetupTestPage();

  const ResultSet expected[4] = {
      {StorageResult::kSuccess, StorageResult::kSuccess,
       StorageResult::kSuccess, StorageResult::kNA},
      {StorageResult::kSuccess, StorageResult::kSuccess,
       StorageResult::kSuccess, StorageResult::kNA},
      {StorageResult::kEmpty, StorageResult::kEmpty, StorageResult::kEmpty,
       StorageResult::kNA},
      {StorageResult::kSuccess, StorageResult::kSuccess,
       StorageResult::kSuccess, StorageResult::kNA},
  };
  TestNewPageResetSession(expected);
}

IN_PROC_BROWSER_TEST_F(EphemeralStorageQaBrowserTest,
                       CookiesAllowedNewPageResetSessionSetPerDomain) {
  // Set the cookie control type to allow for the test page's domain (not
  // browser-wide!).
  SetCookieControlType(brave_shields::ControlType::ALLOW);

  SetupTestPage();

  const ResultSet expected[4] = {
      {StorageResult::kSuccess, StorageResult::kSuccess,
       StorageResult::kSuccess, StorageResult::kNA},
      {StorageResult::kSuccess, StorageResult::kSuccess,
       StorageResult::kSuccess, StorageResult::kNA},
      {StorageResult::kEmpty, StorageResult::kEmpty, StorageResult::kEmpty,
       StorageResult::kNA},
      {StorageResult::kSuccess, StorageResult::kSuccess,
       StorageResult::kSuccess, StorageResult::kNA},
  };
  TestNewPageResetSession(expected);
}
