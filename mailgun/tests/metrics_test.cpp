/// What the component reports, read back the way a scrape reads it.

#include <userver/utest/utest.hpp>
#include <userver/utils/statistics/storage.hpp>
#include <userver/utils/statistics/testing.hpp>

#include <slugkit/mailgun/metrics.hpp>

namespace {

using slugkit::mailgun::Metrics;
using slugkit::mailgun::SendOutcome;
using slugkit::mailgun::SendOutcomeForStatus;
using slugkit::mailgun::VerifyOutcome;
using userver::utils::statistics::Rate;
using userver::utils::statistics::Snapshot;
using userver::utils::statistics::Storage;

auto Read(const Metrics& metrics) -> Snapshot {
    Storage storage;
    auto holder = storage.RegisterWriter("mailgun", [&metrics](auto& writer) { writer = metrics; });
    return Snapshot{storage, "mailgun"};
}

auto Sends(const Snapshot& snapshot, const char* outcome) -> Rate {
    return snapshot.SingleMetric("sends", {{"outcome", outcome}}).AsRate();
}

}  // namespace

TEST(MailgunMetrics, OurFaultAndTheirsAreDifferentOutcomes) {
    EXPECT_EQ(SendOutcomeForStatus(200), SendOutcome::kAccepted);
    // A 4xx is our request, our key, our domain — a different page from a
    // Mailgun outage.
    EXPECT_EQ(SendOutcomeForStatus(401), SendOutcome::kRejected);
    EXPECT_EQ(SendOutcomeForStatus(400), SendOutcome::kRejected);
    EXPECT_EQ(SendOutcomeForStatus(502), SendOutcome::kServerError);
}

UTEST(MailgunMetrics, CountsSendsByOutcome) {
    Metrics metrics;
    metrics.AccountSend(SendOutcome::kAccepted);
    metrics.AccountSend(SendOutcome::kAccepted);
    metrics.AccountSend(SendOutcome::kRejected);
    metrics.AccountSend(SendOutcome::kUnconfigured);
    metrics.AccountSendDuration(std::chrono::milliseconds{120});
    metrics.AccountSendDuration(std::chrono::milliseconds{900});

    const auto snapshot = Read(metrics);
    EXPECT_EQ(Sends(snapshot, "accepted"), Rate{2});
    EXPECT_EQ(Sends(snapshot, "rejected"), Rate{1});
    EXPECT_EQ(Sends(snapshot, "unconfigured"), Rate{1});
    EXPECT_EQ(Sends(snapshot, "transport_error"), Rate{0});
    EXPECT_EQ(snapshot.SingleMetric("send-duration-ms").AsHistogram().GetTotalCount(), 2u);
}

UTEST(MailgunMetrics, EveryOutcomeIsPresentBeforeItHappens) {
    const Metrics metrics;
    const auto snapshot = Read(metrics);
    for (const auto* outcome : {"accepted", "rejected", "server_error", "transport_error", "unconfigured", "invalid"}) {
        EXPECT_EQ(Sends(snapshot, outcome), Rate{0}) << outcome;
    }
    for (const auto* outcome : {"ok", "refused", "unconfigured"}) {
        EXPECT_EQ(snapshot.SingleMetric("verifications", {{"outcome", outcome}}).AsRate(), Rate{0}) << outcome;
    }
    EXPECT_EQ(snapshot.SingleMetric("configured").AsInt(), 0);
}

UTEST(MailgunMetrics, ResetClearsTheCountersNotTheState) {
    Metrics metrics;
    metrics.SetConfigured(true);
    metrics.AccountVerification(VerifyOutcome::kRefused);
    metrics.AccountSend(SendOutcome::kServerError);

    ResetMetric(metrics);

    const auto snapshot = Read(metrics);
    EXPECT_EQ(snapshot.SingleMetric("verifications", {{"outcome", "refused"}}).AsRate(), Rate{0});
    EXPECT_EQ(Sends(snapshot, "server_error"), Rate{0});
    EXPECT_EQ(snapshot.SingleMetric("configured").AsInt(), 1);
}
