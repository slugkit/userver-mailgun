#pragma once

#include <userver/utils/strong_typedef.hpp>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace slugkit::mailgun {

using EmailAddress = userver::utils::StrongTypedef<struct EmailAddressTag, std::string>;
using OptionalEmailAddress = userver::utils::StrongTypedef<struct OptionalEmailAddressTag, std::optional<EmailAddress>>;

using Subject = userver::utils::StrongTypedef<struct SubjectTag, std::string>;
using Text = userver::utils::StrongTypedef<struct TextTag, std::string>;
using Html = userver::utils::StrongTypedef<struct HtmlTag, std::string>;

using OptionalString = std::optional<std::string>;
using EmailList = std::vector<EmailAddress>;
using TemplateData = std::map<std::string, std::string>;
using Tags = std::vector<std::string>;

struct MessageTemplate {
    std::string name;
    TemplateData data;
    OptionalString version;
};

/// @brief Mailgun message
/// A subset of the Mailgun message API
/// New features are added as they are needed
/// @see https://documentation.mailgun.com/docs/mailgun/api-reference/send/mailgun/messages
struct Message {
    OptionalEmailAddress from;
    /// @brief Recipients of the email
    /// @note At least one recipient is required
    EmailList to;

    /// @brief Carbon copy recipients of the email
    /// @note This is optional
    EmailList cc;

    /// @brief Blind carbon copy recipients of the email
    /// @note This is optional
    EmailList bcc;

    /// @brief Subject of the email
    /// @note This is required unless the template provides has a subject
    /// configured
    std::string subject;

    /// @brief Text content of the email
    /// @note This is optional
    OptionalString text;

    /// @brief HTML content of the email
    /// @note This is optional
    OptionalString html;

    /// @brief Template to use for the email
    /// @note This is optional
    std::optional<MessageTemplate> message_template;

    /// @brief Tags to add to the email
    /// @note This is optional
    Tags tags;

    static auto MakeMessage(const EmailAddress& to, MessageTemplate&& template, OptionalString&& subject = std::nullopt)
        -> Message;
    static auto MakeMessage(const EmailAddress& to, Subject&& subject, Text&& text) -> Message;
    static auto MakeMessage(const EmailAddress& to, Subject&& subject, Html&& html) -> Message;
};

}  // namespace slugkit::mailgun