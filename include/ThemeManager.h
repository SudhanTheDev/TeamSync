#pragma once

#include <QIcon>
#include <QString>

class QWidget;

namespace teamsync {

// One source of truth for the desktop palette, widget styles and SVG icons.
// Theme preferences remain in TeamSyncGui so applying a preview never saves it.
class ThemeManager final {
public:
    static void apply(QWidget* window, bool dark);
    static QIcon icon(const QString& name, bool dark, bool accent = false, bool onAccent = false);
    static QIcon applicationIcon();
    static bool systemPrefersDark();
    static void applyNativeTitleBar(QWidget* window, bool dark);
};

} // namespace teamsync
