#pragma once

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QListWidget>
#include <QMimeData>
#include <QObject>
#include <QUrl>
#include <functional>

namespace teamsync::ui {

inline QStringList droppedLocalFiles(const QMimeData* mime) {
    QStringList result;
    if (!mime || !mime->hasUrls()) return result;
    for (const auto& url : mime->urls()) {
        if (!url.isLocalFile()) continue;
        const QFileInfo file(url.toLocalFile());
        if (file.isFile() && !file.isSymLink()) result << file.absoluteFilePath();
    }
    result.removeDuplicates();
    return result;
}

// Child-owned filter: accepting external files never enables internal row moves.
class FileDropFilter final : public QObject {
public:
    FileDropFilter(QWidget* surface, std::function<void(const QStringList&)> callback)
        : QObject(surface), callback_(std::move(callback)) {
        surface->setAcceptDrops(true);
        surface->installEventFilter(this);
    }
protected:
    bool eventFilter(QObject*, QEvent* event) override {
        if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
            auto* drag = static_cast<QDragMoveEvent*>(event);
            if (!droppedLocalFiles(drag->mimeData()).isEmpty()) {
                drag->setDropAction(Qt::CopyAction); drag->accept(); return true;
            }
        } else if (event->type() == QEvent::Drop) {
            auto* drop = static_cast<QDropEvent*>(event);
            const auto files = droppedLocalFiles(drop->mimeData());
            if (!files.isEmpty()) {
                drop->setDropAction(Qt::CopyAction); drop->accept();
                callback_(files); return true;
            }
        }
        return false;
    }
private:
    std::function<void(const QStringList&)> callback_;
};

inline void acceptFileDrops(QWidget* surface, std::function<void(const QStringList&)> callback) {
    new FileDropFilter(surface, std::move(callback));
}
} // namespace teamsync::ui
