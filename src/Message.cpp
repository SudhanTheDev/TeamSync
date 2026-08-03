#include "Message.h"

#include "Exceptions.h"
#include "Utils.h"

#include <algorithm>
#include <ostream>
#include <utility>

namespace teamsync {

int Message::nextId_ = 20001;

Message::Message(const int id, const int teamId, const int senderId, std::string text,
                 std::string dateTime)
    : id_(id), teamId_(teamId), senderId_(senderId), text_(util::trim(text)),
      dateTime_(std::move(dateTime)) {
    if (text_.empty()) throw ValidationError("Message text cannot be empty.");
    observeId(id_);
}

int Message::generateId() { return nextId_++; }
void Message::observeId(const int id) { nextId_ = std::max(nextId_, id + 1); }

std::ostream& operator<<(std::ostream& out, const Message& message) {
    return out << '[' << message.dateTime_ << "] User " << message.senderId_ << ": "
               << message.text_ << " (#" << message.id_ << ')';
}

ChatRoom::ChatRoom(const int teamId, std::vector<Message> messages)
    : teamId_(teamId), messages_(std::move(messages)) {
    messages_.erase(std::remove_if(messages_.begin(), messages_.end(),
                                   [teamId](const Message& value) { return value.teamId() != teamId; }),
                    messages_.end());
}

void ChatRoom::addMessage(const Message& message) {
    if (message.teamId() != teamId_) throw ValidationError("Message belongs to a different team.");
    messages_.push_back(message);
}

bool ChatRoom::deleteOwnMessage(const int messageId, const int requestingUserId) {
    const auto position = std::find_if(messages_.begin(), messages_.end(),
        [messageId](const Message& value) { return value.id() == messageId; });
    if (position == messages_.end()) return false;
    if (position->senderId() != requestingUserId) {
        throw AuthorizationError("You can delete only your own messages.");
    }
    messages_.erase(position);
    return true;
}

std::vector<Message> ChatRoom::search(const std::string& query) const {
    std::vector<Message> results;
    std::copy_if(messages_.begin(), messages_.end(), std::back_inserter(results),
                 [&query](const Message& value) {
                     return util::containsIgnoreCase(value.text(), query);
                 });
    return results;
}

} // namespace teamsync
