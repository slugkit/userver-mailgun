#include <slugkit/mailgun/webhook.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <unordered_map>

#include <userver/crypto/hash.hpp>
#include <userver/utils/datetime.hpp>

namespace slugkit::mailgun {

namespace {

/// Compares in time proportional to the length of the inputs and nothing else.
///
/// The endpoint is public by necessity and an attacker may call it as often as
/// they like, which turns the usual "timing attacks are theoretical" into a
/// practical oracle: a byte-at-a-time compare leaks the signature one byte per
/// batch of requests.
auto ConstantTimeEquals(std::string_view lhs, std::string_view rhs) noexcept -> bool {
    if (lhs.size() != rhs.size()) {
        // Length is not a secret — the digest is a fixed width — so returning
        // early here leaks nothing that is not already public.
        return false;
    }
    unsigned char difference = 0;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        difference |= static_cast<unsigned char>(lhs[i]) ^ static_cast<unsigned char>(rhs[i]);
    }
    return difference == 0;
}

auto Lowered(std::string value) -> std::string {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

const std::unordered_map<std::string, EventKind> kKinds{
    {"accepted", EventKind::kAccepted},
    {"delivered", EventKind::kDelivered},
    {"complained", EventKind::kComplained},
    {"unsubscribed", EventKind::kUnsubscribed},
    {"opened", EventKind::kOpened},
    {"clicked", EventKind::kClicked},
};

}  // namespace

auto ToStringView(EventKind kind) noexcept -> std::string_view {
    switch (kind) {
        case EventKind::kUnknown: return "unknown";
        case EventKind::kAccepted: return "accepted";
        case EventKind::kDelivered: return "delivered";
        case EventKind::kPermanentFailure: return "permanent_failure";
        case EventKind::kTemporaryFailure: return "temporary_failure";
        case EventKind::kComplained: return "complained";
        case EventKind::kUnsubscribed: return "unsubscribed";
        case EventKind::kOpened: return "opened";
        case EventKind::kClicked: return "clicked";
    }
    return "unknown";
}

auto Suppresses(EventKind kind) noexcept -> bool {
    // A temporary failure is deliberately absent: a full mailbox or a
    // greylisting is not a reason to stop writing to somebody for ever, and
    // treating it as one is how a sender loses a correspondent to one bad
    // afternoon at their provider.
    return kind == EventKind::kPermanentFailure || kind == EventKind::kComplained ||
           kind == EventKind::kUnsubscribed;
}

auto ParseSignature(const userver::formats::json::Value& body) -> std::optional<WebhookSignature> {
    const auto signature = body["signature"];
    if (!signature.IsObject()) {
        return std::nullopt;
    }

    WebhookSignature parsed{
        .timestamp = signature["timestamp"].As<std::string>(""),
        .token = signature["token"].As<std::string>(""),
        .signature = signature["signature"].As<std::string>(""),
    };
    if (parsed.timestamp.empty() || parsed.token.empty() || parsed.signature.empty()) {
        return std::nullopt;
    }
    return parsed;
}

auto VerifySignature(const WebhookSignature& signature, std::string_view signing_key, std::chrono::seconds tolerance)
    -> bool {
    if (signing_key.empty()) {
        // An unset key must never mean "accept everything". A deployment that
        // forgot to provision one would otherwise have an open endpoint that
        // anybody could use to suppress its recipients.
        return false;
    }

    if (tolerance.count() != 0) {
        char* end = nullptr;
        const auto seconds = std::strtoll(signature.timestamp.c_str(), &end, 10);
        if (end == signature.timestamp.c_str() || *end != '\0') {
            return false;
        }
        const auto sent = std::chrono::system_clock::from_time_t(static_cast<std::time_t>(seconds));
        const auto drift = userver::utils::datetime::Now() - sent;
        if (drift > tolerance || drift < -tolerance) {
            // A signature with no expiry stays valid for ever, and one captured
            // event could then be replayed to suppress an address permanently.
            return false;
        }
    }

    const auto expected = userver::crypto::hash::HmacSha256(
        signing_key,
        signature.timestamp + signature.token,
        userver::crypto::hash::OutputEncoding::kHex
    );
    return ConstantTimeEquals(Lowered(expected), Lowered(signature.signature));
}

auto ParseEvent(const userver::formats::json::Value& body) -> Event {
    const auto data = body["event-data"];

    Event event;
    event.raw_event = data["event"].As<std::string>("");
    event.recipient = data["recipient"].As<std::string>("");
    event.id = data["id"].As<std::string>("");
    event.reason = data["delivery-status"]["description"].As<std::string>("");
    if (event.reason.empty()) {
        event.reason = data["delivery-status"]["message"].As<std::string>("");
    }
    if (event.reason.empty()) {
        event.reason = data["reason"].As<std::string>("");
    }
    event.message_id = data["message"]["headers"]["message-id"].As<std::string>("");

    const auto tags = data["tags"];
    if (tags.IsArray()) {
        for (const auto& tag : tags) {
            event.tags.push_back(tag.As<std::string>(""));
        }
    }

    // Mailgun sends a float — seconds with a fractional part — so this goes
    // through a double rather than an integer parse.
    const auto timestamp = data["timestamp"].As<double>(0.0);
    event.occurred_at = std::chrono::system_clock::time_point{
        std::chrono::milliseconds{static_cast<std::int64_t>(timestamp * 1000)}
    };

    if (event.raw_event == "failed") {
        // The two-field rule every consumer would otherwise re-derive: `failed`
        // alone does not say whether the address is gone.
        event.kind = data["severity"].As<std::string>("") == "permanent" ? EventKind::kPermanentFailure
                                                                        : EventKind::kTemporaryFailure;
        return event;
    }

    const auto known = kKinds.find(event.raw_event);
    event.kind = known == kKinds.end() ? EventKind::kUnknown : known->second;
    return event;
}

}  // namespace slugkit::mailgun
