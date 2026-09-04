#pragma once

/// @file
/// Mailgun credentials out of the static config and into secdist.
///
/// The API key is a credential, and a credential in a static config is a
/// credential in a file: userver renders config through templates onto disk,
/// so a deployment that fills `api-key` from an environment variable still
/// writes it out. secdist exists for exactly this — it is read once at startup,
/// from a document that never becomes part of the rendered config.
///
/// The expected document, keyed by the component's `secdist-key`:
///
/// ```json
/// {
///   "mailgun": {
///     "api_key":  "…",
///     "domain":   "mg.example.com",
///     "base_url": "https://api.eu.mailgun.net/v3",
///     "from":     "Example <no-reply@mg.example.com>",
///     "webhook_signing_key": "…"
///   }
/// }
/// ```
///
/// Only `api_key` is a secret; the other three are here because they travel
/// with it. A sending domain is the deployment's, changes when the account
/// does, and being able to move the whole set together is what makes rotating
/// an account a secret change rather than a redeploy. Any of the three may be
/// omitted and taken from the static config instead.
///
/// Keyed by name so that a process talking to two Mailgun accounts — a
/// transactional domain and a bulk one — configures two components against two
/// blocks rather than needing a second mechanism.

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include <userver/formats/json/value.hpp>

namespace slugkit::mailgun {

/// One block of the secdist document.
struct Credentials {
    std::string api_key;
    std::optional<std::string> domain;
    std::optional<std::string> base_url;
    std::optional<std::string> from;
    /// A different secret from the API key: Mailgun signs delivery webhooks
    /// with it. Optional, because a service that only sends never verifies one.
    std::optional<std::string> webhook_signing_key;
};

class Secrets final {
public:
    Secrets() = default;
    explicit Secrets(const userver::formats::json::Value& doc);

    /// @returns the block under @p key, or nullptr when the document has none.
    ///
    /// Absence is not an error here. Every component in a process shares one
    /// secdist document, and a service that configures Mailgun through the
    /// static config has no reason to carry a block for it; the component
    /// decides what a missing block means, because only it knows whether it was
    /// told to expect one.
    [[nodiscard]] auto Find(std::string_view key) const -> const Credentials*;

private:
    std::unordered_map<std::string, Credentials> blocks_;
};

}  // namespace slugkit::mailgun
