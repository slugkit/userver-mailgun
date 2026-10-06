#pragma once

/// @file
/// What the component reports about its sends and its webhook checks.
///
/// ```
/// <prefix>.sends             {outcome}  rate
/// <prefix>.send-duration-ms             histogram — requests that reached Mailgun
/// <prefix>.verifications     {outcome}  rate — ok | refused | unconfigured
/// <prefix>.configured                   gauge, 0|1
/// ```
///
/// A send's `outcome` is split so the two failures that look alike from the
/// outside page different people. `rejected` is Mailgun answering 4xx: our
/// request, our credentials, our domain, our fix. `server_error` and
/// `transport_error` are Mailgun or the path to it, and the answer is to wait.
/// `unconfigured` and `invalid` never left the process; they are counted
/// because a message nobody sent is still a message somebody expected.
///
/// Every series is written from the first scrape, zeros included, because a
/// rate alert on a series that starts at the first failure has no history to
/// compare against.

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <userver/server/http/http_status.hpp>
#include <userver/utils/statistics/histogram.hpp>
#include <userver/utils/statistics/rate_counter.hpp>
#include <userver/utils/statistics/writer.hpp>

namespace slugkit::mailgun {

enum class SendOutcome {
    /// Mailgun answered 2xx: queued on its side.
    kAccepted,
    /// Mailgun answered 4xx.
    kRejected,
    /// Mailgun answered 5xx.
    kServerError,
    /// No answer: connection, DNS, timeout.
    kTransportError,
    /// The component is inert and refused before building a request.
    kUnconfigured,
    /// The message itself was unsendable — no recipient.
    kInvalid,
};

enum class VerifyOutcome {
    kOk,
    /// A bad signature or a timestamp outside the window. One outcome, because
    /// `VerifySignature` answers one bool and inventing the split here would
    /// be guessing.
    kRefused,
    /// No signing key, so nothing could be verified.
    kUnconfigured,
};

/// The send outcome of an HTTP status Mailgun answered with.
/// Whether a status is in the 2xx class.
///
/// One predicate because two places ask it — the send path and the outcome
/// mapping — and `status / 100 == 2` written twice is two spellings of one
/// rule. `kMultipleChoices` (300) is the first status that is not a success,
/// so it bounds the class without being in it.
[[nodiscard]] constexpr auto IsSuccess(int status) noexcept -> bool {
    return status >= userver::server::http::HttpStatus::kOk
        && status < userver::server::http::HttpStatus::kMultipleChoices;
}

[[nodiscard]] auto SendOutcomeForStatus(int status) noexcept -> SendOutcome;

[[nodiscard]] auto MetricLabel(SendOutcome outcome) noexcept -> std::string_view;
[[nodiscard]] auto MetricLabel(VerifyOutcome outcome) noexcept -> std::string_view;

class Metrics final {
public:
    Metrics();

    void AccountSend(SendOutcome outcome) noexcept;
    /// How long a request that reached Mailgun took, whatever it answered.
    void AccountSendDuration(std::chrono::milliseconds elapsed) noexcept;
    void AccountVerification(VerifyOutcome outcome) noexcept;
    void SetConfigured(bool configured) noexcept { configured_.store(configured ? 1 : 0); }

    friend void DumpMetric(userver::utils::statistics::Writer& writer, const Metrics& metrics);
    friend void ResetMetric(Metrics& metrics);

private:
    std::array<userver::utils::statistics::RateCounter, 6> sends_{};
    std::array<userver::utils::statistics::RateCounter, 3> verifications_{};
    userver::utils::statistics::Histogram send_duration_ms_;
    std::atomic<std::int64_t> configured_{0};
};

}  // namespace slugkit::mailgun
