#pragma once

#include <slugkit/mailgun/types.hpp>
#include <slugkit/mailgun/webhook.hpp>

#include <userver/components/component_base.hpp>
#include <userver/utils/fast_pimpl.hpp>

#include <string_view>

namespace slugkit::mailgun {

/// @brief Sends mail through Mailgun, and verifies what Mailgun sends back.
///
/// ### Credentials
///
/// Two ways in, and the second is the one to prefer:
///
///   * **static config** — `api-key`, `domain`, `from`, `base-url`. Simple, and
///     fine for a development run.
///   * **secdist** — set `secdist-key` and the component reads its credentials
///     from that block of the secdist document instead (slugkit/mailgun/
///     secrets.hpp has the shape). An API key in a static config is an API key
///     in a file: userver renders config through templates onto disk, so even a
///     value that arrives in an environment variable is written out. secdist is
///     read once, from a document that never becomes part of the rendered
///     config.
///
/// secdist wins where it provides a value; the static config fills in what it
/// does not, so a deployment can keep its sending domain in plain config and
/// its key out of one.
///
/// Missing credentials stop the process by default, which is right where mail
/// is what the process is for. Where it is one channel of several, set
/// `credentials-optional` and the component starts **inert** instead: @ref
/// Configured is false, @ref Send throws, and the service boots and serves
/// everything else. An environment that has not been provisioned yet should be
/// able to run with mail switched off, not be unable to start.
///
/// ### Webhooks
///
/// The signing key is a *different* secret from the API key, and lives beside
/// it. @ref VerifyWebhook is the component's half of receiving an event —
/// signature and freshness — because the scheme is Mailgun's knowledge and a
/// service that had learnt it would have to be edited when Mailgun changes it.
/// Parsing is @ref ParseEvent, which is free-standing: it needs no credentials.
class Mailgun : public userver::components::ComponentBase {
public:
    static constexpr std::string_view kName = "mailgun";

    Mailgun(const userver::components::ComponentConfig& config, const userver::components::ComponentContext& context);
    /// @note Declared out of line because Impl is incomplete here: FastPimpl needs the destructor
    ///       defined where the implementation is visible.
    ~Mailgun() override;

    static auto GetStaticConfigSchema() -> userver::yaml_config::Schema;

    void Send(EmailAddress&& to, Subject&& subject, Text&& text) const;
    void Send(const Message& message) const;

    /// Verifies a webhook's signature against the configured signing key.
    ///
    /// @returns false when no signing key is configured. An unset key must
    ///          never mean "accept everything": the endpoint is public, and a
    ///          deployment that forgot to provision one would otherwise let
    ///          anybody suppress its recipients.
    [[nodiscard]] auto VerifyWebhook(const WebhookSignature& signature) const -> bool;

    /// Whether a signing key was configured at all, so a service can refuse to
    /// register the endpoint rather than serve one that rejects everything.
    [[nodiscard]] auto CanVerifyWebhooks() const -> bool;

    /// Whether credentials resolved at startup.
    ///
    /// Always true unless `credentials-optional` is set — without it the
    /// component throws instead of constructing. With it, a caller checks this
    /// once and reports the channel unavailable rather than calling @ref Send
    /// and catching.
    [[nodiscard]] auto Configured() const -> bool;

    /// What is missing, when @ref Configured is false; empty otherwise.
    ///
    /// A sentence, safe to log: it names the setting that is absent and never
    /// the value of one that is present.
    [[nodiscard]] auto UnconfiguredReason() const -> std::string_view;

private:
    constexpr static auto kImplSize = 232UL;
    constexpr static auto kImplAlign = 8UL;
    struct Impl;
    userver::utils::FastPimpl<Impl, kImplSize, kImplAlign> impl_;
};

}  // namespace slugkit::mailgun