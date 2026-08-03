#pragma once

#include <iosfwd>
#include <string>

namespace teamsync {

class Notification {
public:
    Notification() = default;
    Notification(int id, int userId, std::string text, std::string dateTime, bool read = false);

    int id() const { return id_; }
    int userId() const { return userId_; }
    const std::string& text() const { return text_; }
    const std::string& dateTime() const { return dateTime_; }
    bool isRead() const { return read_; }
    void markRead() { read_ = true; }

    static int generateId();
    static void observeId(int id);
    friend std::ostream& operator<<(std::ostream& out, const Notification& notification);

private:
    int id_ = 0;
    int userId_ = 0;
    std::string text_;
    std::string dateTime_;
    bool read_ = false;
    static int nextId_;
};

} // namespace teamsync
