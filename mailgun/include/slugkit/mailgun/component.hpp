#pragma once

#include <slugkit/mailgun/types.hpp>

#include <userver/components/component_base.hpp>
#include <userver/utils/fast_pimpl.hpp>

namespace slugkit::mailgun {

class Mailgun : public userver::components::ComponentBase {
public:
    static constexpr std::string_view kName = "mailgun";

    Mailgun(const userver::components::ComponentConfig& config, const userver::components::ComponentContext& context);

    static auto GetStaticConfigSchema() -> userver::yaml_config::Schema;

    void Send(EmailAddress&& to, Subject&& subject, Text&& text) const;
    void Send(const Message& message) const;

private:
    constexpr static auto kImplSize = 144UL;
    constexpr static auto kImplAlign = 8UL;
    struct Impl;
    userver::utils::FastPimpl<Impl, kImplSize, kImplAlign> impl_;
};

}  // namespace slugkit::mailgun