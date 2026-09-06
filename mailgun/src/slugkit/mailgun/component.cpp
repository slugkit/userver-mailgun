#include <slugkit/mailgun/component.hpp>

#include <slugkit/mailgun/secrets.hpp>

#include <userver/clients/http/client.hpp>
#include <userver/clients/http/component.hpp>
#include <userver/clients/http/form.hpp>
#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/crypto/base64.hpp>
#include <userver/storages/secdist/component.hpp>
#include <userver/storages/secdist/secdist.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/formats/serialize/common_containers.hpp>
#include <userver/formats/serialize/to.hpp>
#include <userver/logging/log.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

#include <fmt/format.h>

#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace slugkit::mailgun {

namespace {

/// @brief Mailgun takes multiple recipients as one comma-separated field value.
auto JoinAddresses(const EmailList& addresses) -> std::string {
    std::string result;
    for (const auto& address : addresses) {
        if (!result.empty()) {
            result += ", ";
        }
        result += address.GetUnderlying();
    }
    return result;
}

}  // namespace

struct Mailgun::Impl {
    userver::components::HttpClient& http_client_;
    std::string base_url_;
    std::string domain_;
    std::string authn_;
    std::string from_;
    std::string webhook_signing_key_;
    std::chrono::seconds webhook_tolerance_;
    std::chrono::milliseconds timeout_;
    /// Empty when the component is configured; otherwise what is missing.
    std::string unconfigured_;

    Impl(const userver::components::ComponentConfig& config, const userver::components::ComponentContext& context)
        : http_client_(context.FindComponent<userver::components::HttpClient>())
        , webhook_tolerance_(config["webhook-tolerance"].As<std::chrono::seconds>(std::chrono::minutes{15}))
        , timeout_(config["timeout"].As<std::chrono::milliseconds>(std::chrono::milliseconds{30000})) {
        auto missing = Configure(config, context);
        if (!missing.has_value()) {
            return;
        }

        if (!config["credentials-optional"].As<bool>(false)) {
            throw std::runtime_error{fmt::format("mailgun: {}", *missing)};
        }

        // Inert rather than fatal, because the deployment asked for that. A
        // process is more than its mail: refusing to start because one provider
        // has not been provisioned yet takes down everything else in the binary,
        // and where mail is one channel of several the honest degradation is a
        // channel that reports itself unavailable.
        //
        // Loudly, though — at error level, once, naming what is missing. Silent
        // inertness is the worse failure of the two: a deployment that looks
        // healthy and quietly sends nothing.
        unconfigured_ = std::move(*missing);
        LOG_ERROR() << "mailgun: inert — " << unconfigured_
                    << ". The component started because credentials-optional is set; every send will be refused "
                       "until it is configured.";
    }

    /// Resolves credentials from secdist, then from the static config.
    ///
    /// @returns nothing when the component is configured, and otherwise the
    ///          first thing found missing, phrased for a log line or an
    ///          exception message. Never a value: a message that named the key
    ///          it found would be the key in a log.
    auto Configure(
        const userver::components::ComponentConfig& config,
        const userver::components::ComponentContext& context
    ) -> std::optional<std::string> {
        // secdist first, static config behind it. A deployment may keep its
        // sending domain in plain config and its key out of one, so this is a
        // per-field fallback rather than a choice between two sources.
        const Credentials* secret = nullptr;
        Secrets secrets;
        const auto secdist_key = config["secdist-key"].As<std::string>("");
        if (!secdist_key.empty()) {
            // Looked up only when asked for: a service configured entirely from
            // static config need not have a secdist component at all, and
            // FindComponent would throw at startup if it did not.
            secrets = context.FindComponent<userver::components::Secdist>().Get().Get<Secrets>();
            secret = secrets.Find(secdist_key);
            if (secret == nullptr) {
                return fmt::format(
                    "secdist-key '{}' is configured, but the secdist document has no such block", secdist_key
                );
            }
        }

        const auto from_secdist = [&](const std::optional<std::string>& value) -> std::optional<std::string> {
            return secret != nullptr ? value : std::nullopt;
        };

        base_url_ = from_secdist(secret != nullptr ? secret->base_url : std::nullopt)
                        .value_or(config["base-url"].As<std::string>("https://api.mailgun.net/v3"));
        domain_ = from_secdist(secret != nullptr ? secret->domain : std::nullopt)
                      .value_or(config["domain"].As<std::string>(""));
        from_ =
            from_secdist(secret != nullptr ? secret->from : std::nullopt).value_or(config["from"].As<std::string>(""));

        if (domain_.empty()) {
            return "no sending domain, in secdist or in the static config";
        }

        // Required, like the domain. `from` was mandatory before secdist support
        // made every field fall back, and losing that was a regression with a
        // bad shape: an empty From is not refused by this component, it is
        // refused by Mailgun, as a 400 on the first send — which reads like a
        // malformed message rather than a missing setting, and only appears
        // once somebody tries to send something.
        if (from_.empty()) {
            return "no From address, in secdist or in the static config";
        }

        // The messages path is appended with a leading slash, so a trailing one would double it up.
        while (!base_url_.empty() && base_url_.back() == '/') {
            base_url_.pop_back();
        }

        const auto api_key = secret != nullptr ? secret->api_key : config["api-key"].As<std::string>("");
        if (api_key.empty()) {
            return "no api key, in secdist or in the static config";
        }
        authn_ =
            fmt::format("Basic {}", userver::crypto::base64::Base64Encode(fmt::format("api:{}", api_key)));

        // A separate secret from the API key, and optional: a service that only
        // sends never receives an event, and demanding a key it will not use
        // would make provisioning harder for no gain.
        webhook_signing_key_ = secret != nullptr ? secret->webhook_signing_key.value_or("")
                                                 : config["webhook-signing-key"].As<std::string>("");
        return std::nullopt;
    }

    void SendForm(userver::clients::http::Form&& form) const {
        auto url = fmt::format("{}/{}/messages", base_url_, domain_);
        auto response = http_client_.GetHttpClient()
                            .CreateRequest()
                            .post(url, std::move(form))
                            .timeout(timeout_)
                            .headers({{"Authorization", authn_}, {"Content-Type", "multipart/form-data"}})
                            .perform();
        if (response->status_code() / 100 != 2) {
            LOG_ERROR() << fmt::format("Failed to send email: {}", response->status_code());
            throw std::runtime_error(fmt::format("Failed to send email: {}", response->status_code()));
        }
        LOG_INFO() << "Email sent, response: " << response->body();
    }

    void Send(const Message& message) const {
        if (!unconfigured_.empty()) {
            // A caller that checks Configured() never reaches this. One that
            // does not gets an exception rather than a silent drop, because a
            // message nobody was told about is worse than one that failed.
            throw std::runtime_error{fmt::format("mailgun: not configured — {}", unconfigured_)};
        }
        if (message.to.empty()) {
            throw std::runtime_error("Mailgun message must have at least one recipient");
        }

        userver::clients::http::Form form;

        const auto& from = message.from.GetUnderlying();
        form.AddContent("from", from.has_value() ? from->GetUnderlying() : from_);
        form.AddContent("to", JoinAddresses(message.to));
        if (!message.cc.empty()) {
            form.AddContent("cc", JoinAddresses(message.cc));
        }
        if (!message.bcc.empty()) {
            form.AddContent("bcc", JoinAddresses(message.bcc));
        }
        // Optional, and only when it says something. Mailgun has no field for
        // this, so it goes as a header override; an empty one would be a
        // `Reply-To:` that mail clients render and that sends the reply
        // nowhere, which is worse than omitting the header.
        const auto& reply_to = message.reply_to.GetUnderlying();
        if (reply_to.has_value() && !reply_to->GetUnderlying().empty()) {
            form.AddContent("h:Reply-To", reply_to->GetUnderlying());
        }
        // A template can carry its own subject, so an empty one is not an error here.
        if (!message.subject.empty()) {
            form.AddContent("subject", message.subject);
        }
        if (message.text.has_value()) {
            form.AddContent("text", *message.text);
        }
        if (message.html.has_value()) {
            form.AddContent("html", *message.html);
        }
        if (message.message_template.has_value()) {
            form.AddContent("template", message.message_template->name);
            if (!message.message_template->data.empty()) {
                const auto data = Serialize(
                    message.message_template->data, userver::formats::serialize::To<userver::formats::json::Value>()
                );
                form.AddContent("h:X-Mailgun-Variables", ToString(data));
            }
            if (message.message_template->version.has_value()) {
                form.AddContent("t:version", *message.message_template->version);
            }
        }
        for (const auto& tag : message.tags) {
            form.AddContent("o:tag", tag);
        }

        SendForm(std::move(form));
    }
};

Mailgun::Mailgun(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context
)
    : ComponentBase{config, context}
    , impl_{config, context} {
}

Mailgun::~Mailgun() = default;

void Mailgun::Send(EmailAddress&& to, Subject&& subject, Text&& text) const {
    impl_->Send(Message::MakeMessage(to, std::move(subject), std::move(text)));
}

void Mailgun::Send(const Message& message) const {
    impl_->Send(message);
}

auto Mailgun::VerifyWebhook(const WebhookSignature& signature) const -> bool {
    return slugkit::mailgun::VerifySignature(signature, impl_->webhook_signing_key_, impl_->webhook_tolerance_);
}

auto Mailgun::CanVerifyWebhooks() const -> bool {
    return !impl_->webhook_signing_key_.empty();
}

auto Mailgun::Configured() const -> bool {
    return impl_->unconfigured_.empty();
}

auto Mailgun::UnconfiguredReason() const -> std::string_view {
    return impl_->unconfigured_;
}

auto Mailgun::GetStaticConfigSchema() -> userver::yaml_config::Schema {
    return userver::yaml_config::MergeSchemas<userver::components::ComponentBase>(R"(
type: object
description: Mailgun component
additionalProperties: false
properties:
    base-url:
        type: string
        description: Base URL for the Mailgun API
        defaultDescription: https://api.mailgun.net/v3
    credentials-optional:
        type: boolean
        description: >-
            Whether the component may start without resolvable credentials.
            When it may, an absent secdist block — or a missing domain, From
            address or api key — leaves the component *inert* instead of
            stopping the process: Configured() is false, UnconfiguredReason()
            says what is missing, and Send throws. For a service where mail is
            one channel of several, that is the honest degradation; leave it
            false where mail is the point of the process.
        defaultDescription: false, meaning missing credentials stop the process
    secdist-key:
        type: string
        description: >-
            Block of the secdist document to read credentials from. Preferred
            over the fields below: an api-key in a static config is an api-key
            in a rendered file. Where secdist supplies a value it wins; the
            fields below fill in the rest.
        defaultDescription: unset, meaning static config only
    api-key:
        type: string
        description: API key for the Mailgun API, when not using secdist
    domain:
        type: string
        description: Domain the messages are sent from
    from:
        type: string
        description: Default From address, used when a message does not set one
    webhook-signing-key:
        type: string
        description: >-
            Key Mailgun signs delivery webhooks with — a different secret from
            the API key. Optional: a service that only sends never verifies one.
        defaultDescription: unset, meaning webhooks cannot be verified
    webhook-tolerance:
        type: string
        description: >-
            How far a webhook's timestamp may be from now before it is refused
            as a replay.
        defaultDescription: 15m
    timeout:
        type: string
        description: Timeout for a single Mailgun API request
        defaultDescription: 30s
    )");
}

}  // namespace slugkit::mailgun
