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

/// @brief One file attached to a message.
/// @note Travels as an `attachment` multipart part of the messages call; the
///       bytes are held by value because the form the client builds keeps
///       its own copy and the message may outlive the caller's buffer.
struct Attachment {
    /// @brief The name the recipient's client shows and saves under.
    std::string filename;
    /// @brief The part's media type, e.g. `image/jpeg`. Empty lets the
    ///        client fall back to `application/octet-stream`.
    std::string content_type;
    /// @brief The file, verbatim.
    std::string data;
};

using Attachments = std::vector<Attachment>;

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

    /// @brief Where replies to this message should go
    /// @note This is optional, and omitted when unset — Mailgun has no
    ///       first-class field for it, so it travels as the `h:Reply-To`
    ///       header override. An empty address is treated as unset rather
    ///       than sent: a `Reply-To:` with nothing in it is rendered by mail
    ///       clients and sends the reply nowhere, which is worse than having
    ///       no header at all.
    OptionalEmailAddress reply_to;

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

    /// @brief Files attached to the email
    /// @note This is optional. Each one is an `attachment` multipart part;
    ///       Mailgun's size ceiling for the whole message (25 MB) is the
    ///       provider's to enforce, not this client's.
    Attachments attachments;

    static auto
    MakeMessage(const EmailAddress& to, MessageTemplate&& message_template, OptionalString&& subject = std::nullopt)
        -> Message;
    static auto MakeMessage(const EmailAddress& to, Subject&& subject, Text&& text) -> Message;
    static auto MakeMessage(const EmailAddress& to, Subject&& subject, Html&& html) -> Message;
};

}  // namespace slugkit::mailgun