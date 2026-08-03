#include "TaskAttachment.h"

#include "Exceptions.h"
#include "Utils.h"

#include <algorithm>
#include <utility>

namespace teamsync {

int TaskAttachment::nextId_ = 40001;

TaskAttachment::TaskAttachment(const int id, const int taskId, const int teamId,
                               const int uploaderId, std::string fileName,
                               std::filesystem::path relativePath, std::string description,
                               std::string dateAdded, const std::uintmax_t size)
    : id_(id), taskId_(taskId), teamId_(teamId), uploaderId_(uploaderId),
      fileName_(util::trim(fileName)), relativePath_(std::move(relativePath)),
      description_(std::move(description)), dateAdded_(std::move(dateAdded)), size_(size) {
    if (fileName_.empty()) throw ValidationError("Attachment file name cannot be empty.");
    if (relativePath_.empty() || relativePath_.is_absolute()) {
        throw ValidationError("Attachment must use a safe workspace-relative path.");
    }
    for (const auto& part : relativePath_) {
        if (part == "..") throw ValidationError("Attachment path cannot leave the workspace.");
    }
    observeId(id_);
}

int TaskAttachment::generateId() { return nextId_++; }
void TaskAttachment::observeId(const int id) { nextId_ = std::max(nextId_, id + 1); }

} // namespace teamsync
