#include <slugkit/mailgun/secrets.hpp>

#include <stdexcept>

#include <fmt/format.h>

namespace slugkit::mailgun {

namespace {

/// An absent field and an empty one are the same thing: nothing was configured.
/// Returning an empty optional for both keeps the component's fallback to the
/// static config from depending on which of the two a deployment produced.
auto Optional(const userver::formats::json::Value& value) -> std::optional<std::string> {
    auto text = value.As<std::string>("");
    if (text.empty()) {
        return std::nullopt;
    }
    return text;
}

}  // namespace

Secrets::Secrets(const userver::formats::json::Value& doc) {
    for (auto it = doc.begin(); it != doc.end(); ++it) {
        const auto& block = *it;
        if (!block.IsObject()) {
            // Not every key in a shared secdist document is ours. A service
            // that keeps its database DSNs beside our block should not have to
            // care that we iterate the whole thing.
            continue;
        }

        const auto api_key = block["api_key"];
        if (api_key.IsMissing()) {
            continue;
        }

        auto value = api_key.As<std::string>("");
        if (value.empty()) {
            // Louder than a missing block, and deliberately: a present-but-empty
            // key is a provisioning step somebody started and did not finish,
            // and it would otherwise fail on the first send as a 401 that reads
            // like an outage.
            throw std::runtime_error{fmt::format("secdist: mailgun block '{}' has an empty api_key", it.GetName())};
        }

        blocks_.emplace(
            it.GetName(),
            Credentials{
                .api_key = std::move(value),
                .domain = Optional(block["domain"]),
                .base_url = Optional(block["base_url"]),
                .from = Optional(block["from"]),
                .webhook_signing_key = Optional(block["webhook_signing_key"]),
            }
        );
    }
}

auto Secrets::Find(std::string_view key) const -> const Credentials* {
    const auto it = blocks_.find(std::string{key});
    return it == blocks_.end() ? nullptr : &it->second;
}

}  // namespace slugkit::mailgun
