#include <slugkit/mailgun/component.hpp>

#include <userver/clients/http/client.hpp>
#include <userver/clients/http/component.hpp>
#include <userver/clients/http/form.hpp>
#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/crypto/base64.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/formats/serialize/common_containers.hpp>
#include <userver/formats/serialize/to.hpp>
#include <userver/logging/log.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

#include <fmt/format.h>

#include <chrono>
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
    std::chrono::milliseconds timeout_;

    Impl(const userver::components::ComponentConfig& config, const userver::components::ComponentContext& context)
        : http_client_(context.FindComponent<userver::components::HttpClient>())
        , base_url_(config["base-url"].As<std::string>("https://api.mailgun.net/v3"))
        , domain_(config["domain"].As<std::string>())
        , from_(config["from"].As<std::string>())
        , timeout_(config["timeout"].As<std::chrono::milliseconds>(std::chrono::milliseconds{30000})) {
        // The messages path is appended with a leading slash, so a trailing one would double it up.
        while (!base_url_.empty() && base_url_.back() == '/') {
            base_url_.pop_back();
        }
        authn_ = fmt::format(
            "Basic {}",
            userver::crypto::base64::Base64Encode(fmt::format("api:{}", config["api-key"].As<std::string>()))
        );
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
    api-key:
        type: string
        description: API key for the Mailgun API
    domain:
        type: string
        description: Domain the messages are sent from
    from:
        type: string
        description: Default From address, used when a message does not set one
    timeout:
        type: string
        description: Timeout for a single Mailgun API request
        defaultDescription: 30s
    )");
}

}  // namespace slugkit::mailgun
