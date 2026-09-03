#include <slugkit/mailgun/types.hpp>

#include <utility>

namespace slugkit::mailgun {

auto Message::MakeMessage(const EmailAddress& to, MessageTemplate&& message_template, OptionalString&& subject)
    -> Message {
    Message message{};
    message.to.push_back(to);
    message.message_template = std::move(message_template);
    // Left empty when absent: the template supplies the subject in that case.
    if (subject.has_value()) {
        message.subject = std::move(*subject);
    }
    return message;
}

auto Message::MakeMessage(const EmailAddress& to, Subject&& subject, Text&& text) -> Message {
    Message message{};
    message.to.push_back(to);
    message.subject = std::move(subject).GetUnderlying();
    message.text = std::move(text).GetUnderlying();
    return message;
}

auto Message::MakeMessage(const EmailAddress& to, Subject&& subject, Html&& html) -> Message {
    Message message{};
    message.to.push_back(to);
    message.subject = std::move(subject).GetUnderlying();
    message.html = std::move(html).GetUnderlying();
    return message;
}

}  // namespace slugkit::mailgun
