#include <slugkit/mailgun/metrics.hpp>

#include <userver/utils/statistics/labels.hpp>

namespace slugkit::mailgun {

namespace {

/// Milliseconds. A Mailgun send is usually a few hundred; an attachment can
/// take seconds, and the client timeout is thirty.
constexpr std::array<double, 10> kDurationBucketsMs{50, 100, 250, 500, 1000, 2500, 5000, 10000, 20000, 30000};

constexpr std::array kSendOutcomes{
    SendOutcome::kAccepted,       SendOutcome::kRejected,     SendOutcome::kServerError,
    SendOutcome::kTransportError, SendOutcome::kUnconfigured, SendOutcome::kInvalid,
};
constexpr std::array kVerifyOutcomes{VerifyOutcome::kOk, VerifyOutcome::kRefused, VerifyOutcome::kUnconfigured};

template <typename Enum>
auto Index(Enum outcome) noexcept -> std::size_t {
    return static_cast<std::size_t>(outcome);
}

}  // namespace

auto SendOutcomeForStatus(int status) noexcept -> SendOutcome {
    if (status / 100 == 2) return SendOutcome::kAccepted;
    if (status >= 500) return SendOutcome::kServerError;
    return SendOutcome::kRejected;
}

auto MetricLabel(SendOutcome outcome) noexcept -> std::string_view {
    switch (outcome) {
        case SendOutcome::kAccepted:
            return "accepted";
        case SendOutcome::kRejected:
            return "rejected";
        case SendOutcome::kServerError:
            return "server_error";
        case SendOutcome::kTransportError:
            return "transport_error";
        case SendOutcome::kUnconfigured:
            return "unconfigured";
        case SendOutcome::kInvalid:
            return "invalid";
    }
    return "rejected";
}

auto MetricLabel(VerifyOutcome outcome) noexcept -> std::string_view {
    switch (outcome) {
        case VerifyOutcome::kOk:
            return "ok";
        case VerifyOutcome::kRefused:
            return "refused";
        case VerifyOutcome::kUnconfigured:
            return "unconfigured";
    }
    return "refused";
}

Metrics::Metrics() : send_duration_ms_{kDurationBucketsMs} {}

void Metrics::AccountSend(SendOutcome outcome) noexcept { ++sends_[Index(outcome)]; }

void Metrics::AccountSendDuration(std::chrono::milliseconds elapsed) noexcept {
    send_duration_ms_.Account(static_cast<double>(elapsed.count()));
}

void Metrics::AccountVerification(VerifyOutcome outcome) noexcept { ++verifications_[Index(outcome)]; }

void DumpMetric(userver::utils::statistics::Writer& writer, const Metrics& metrics) {
    using userver::utils::statistics::LabelView;
    for (const auto outcome : kSendOutcomes) {
        writer["sends"].ValueWithLabels(metrics.sends_[Index(outcome)], LabelView{"outcome", MetricLabel(outcome)});
    }
    writer["send-duration-ms"] = metrics.send_duration_ms_;
    for (const auto outcome : kVerifyOutcomes) {
        writer["verifications"].ValueWithLabels(
            metrics.verifications_[Index(outcome)], LabelView{"outcome", MetricLabel(outcome)}
        );
    }
    writer["configured"] = metrics.configured_.load();
}

void ResetMetric(Metrics& metrics) {
    // The counters only: `configured` is the component's state, not something
    // that accumulated.
    for (auto& counter : metrics.sends_) ResetMetric(counter);
    for (auto& counter : metrics.verifications_) ResetMetric(counter);
    ResetMetric(metrics.send_duration_ms_);
}

}  // namespace slugkit::mailgun
