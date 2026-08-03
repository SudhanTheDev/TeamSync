#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace teamsync {

class Message {
public:
    Message() = default;
    Message(int id, int teamId, int senderId, std::string text, std::string dateTime);

    int id() const { return id_; }
    int teamId() const { return teamId_; }
    int senderId() const { return senderId_; }
    const std::string& text() const { return text_; }
    const std::string& dateTime() const { return dateTime_; }

    static int generateId();
    static void observeId(int id);
    friend std::ostream& operator<<(std::ostream& out, const Message& message);

private:
    int id_ = 0;
    int teamId_ = 0;
    int senderId_ = 0;
    std::string text_;
    std::string dateTime_;
    static int nextId_;
};

class ChatRoom {
public:
    explicit ChatRoom(int teamId = 0) : teamId_(teamId) {}
    ChatRoom(int teamId, std::vector<Message> messages);

    int teamId() const { return teamId_; }
    const std::vector<Message>& messages() const { return messages_; }
    void addMessage(const Message& message);
    bool deleteOwnMessage(int messageId, int requestingUserId);
    std::vector<Message> search(const std::string& query) const;

private:
    int teamId_;
    std::vector<Message> messages_;
};

} // namespace teamsync
