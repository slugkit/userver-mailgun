#pragma once

/// @file
/// Mailgun's delivery webhooks: verified, and normalised into something a
/// caller can act on without learning Mailgun's vocabulary.
///
/// This belongs beside the sender rather than in each service that receives
/// events, for the reason the sender itself does: it is provider knowledge.
/// The signature scheme, the payload's shape and the event names are Mailgun's,
/// they change when Mailgun changes them, and a service that had learnt them
/// would have to be found and edited when they do.
///
/// ### The signature
///
/// Mailgun signs `timestamp + token` with the **webhook signing key**, which is
/// a different secret from the API key, and sends the three together:
///
/// ```json
/// { "signature": { "timestamp": "1529006854", "token": "a8ce0edb…", "signature": "d2271d12…" },
///   "event-data": { "event": "failed", … } }
/// ```
///
/// @ref VerifySignature is a constant-time comparison, and that matters here
/// rather than being a reflex: the endpoint is public by necessity, an attacker
/// can call it as often as they like, and a byte-at-a-time compare over a
/// public endpoint is a practical oracle rather than a theoretical one.
///
/// The timestamp is checked against a tolerance, because a valid signature
/// stays valid for ever otherwise: an event captured once could be replayed to
/// suppress an address permanently.
///
/// ### The events
///
/// Normalised to @ref EventKind because the interesting distinction is not
/// Mailgun's `event` string but what a caller must *do*: a permanent failure
/// suppresses an address, a temporary one does not. Mailgun expresses that as
/// `event: failed` plus `severity: permanent`, which is exactly the kind of
/// two-field rule every consumer would otherwise re-derive.
///
/// Unknown event names parse as @ref EventKind::kUnknown rather than throwing.
/// Mailgun adds event types; a webhook endpoint that 500s on one it has not
/// heard of teaches Mailgun to retry it, and then to stop delivering any.

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <userver/formats/json/value.hpp>

namespace slugkit::mailgun {

/// The signature triple Mailgun sends alongside every event.
struct WebhookSignature {
    std::string timestamp;
    std::string token;
    std::string signature;
};

/// What happened, in terms a caller can act on.
enum class EventKind {
    kUnknown,
    kAccepted,
    kDelivered,
    /// The address is gone. Suppress it.
    kPermanentFailure,
    /// A greylisting, a full mailbox, a provider hiccup. Do not suppress.
    kTemporaryFailure,
    /// Marked as spam. Suppress, and more urgently than a bounce.
    kComplained,
    kUnsubscribed,
    kOpened,
    kClicked,
};

auto ToStringView(EventKind kind) noexcept -> std::string_view;

/// Whether this event means the address must not be written to again.
///
/// The one question most callers have, answered once here so that three
/// services do not each decide whether a temporary failure counts.
[[nodiscard]] auto Suppresses(EventKind kind) noexcept -> bool;

struct Event {
    EventKind kind{EventKind::kUnknown};
    /// Mailgun's own name for it, kept for logs and for the kinds we do not
    /// model — an operator reading `unknown` wants to know what it was.
    std::string raw_event;
    std::string recipient;
    /// Mailgun's event id, for deduplicating retries.
    std::string id;
    /// The `Message-Id` of the message this is about, when the payload carries
    /// one.
    std::string message_id;
    /// The tags the message was sent with. The only handle a sender controls,
    /// and therefore the usual way an event is tied back to whatever caused it.
    std::vector<std::string> tags;
    /// The provider's explanation, for an operator. Not for a recipient.
    std::string reason;
    std::chrono::system_clock::time_point occurred_at;
};

/// Pulls the signature triple out of a webhook body.
///
/// @returns nullopt when the body is not shaped like a Mailgun webhook at all,
/// which a caller should answer with 400 rather than treat as a bad signature.
[[nodiscard]] auto ParseSignature(const userver::formats::json::Value& body) -> std::optional<WebhookSignature>;

/// Verifies @p signature against @p signing_key.
///
/// @param tolerance how far the event's timestamp may be from now. Zero
///        disables the check, which is for tests replaying a captured payload
///        and not for a deployment.
[[nodiscard]] auto VerifySignature(
    const WebhookSignature& signature,
    std::string_view signing_key,
    std::chrono::seconds tolerance = std::chrono::minutes{15}
) -> bool;

/// Parses the `event-data` half. Assumes the signature has already been
/// verified — this function trusts what it is given, and the argument order of
/// the two is the reminder.
[[nodiscard]] auto ParseEvent(const userver::formats::json::Value& body) -> Event;

}  // namespace slugkit::mailgun
