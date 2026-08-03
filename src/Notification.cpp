#include "Notification.h"

#include "Exceptions.h"
#include "Utils.h"

#include <algorithm>
#include <ostream>
#include <utility>

namespace teamsync {

int Notification::nextId_ = 40001;

Notification::Notification(const int id, const int userId, std::string text,
                           std::string dateTime, const bool read)
    : id_(id), userId_(userId), text_(util::trim(text)), dateTime_(std::move(dateTime)),
      read_(read) {
    if (text_.empty()) throw ValidationError("Notification text cannot be empty.");
    observeId(id_);
}

int Notification::generateId() { return nextId_++; }
void Notification::observeId(const int id) { nextId_ = std::max(nextId_, id + 1); }

std::ostream& operator<<(std::ostream& out, const Notification& notification) {
    return out << (notification.read_ ? "[Read] " : "[New] ") << notification.dateTime_
               << " - " << notification.text_ << " (#" << notification.id_ << ')';
}

} // namespace teamsync
