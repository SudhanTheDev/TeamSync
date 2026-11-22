#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QVariant>

namespace teamsync {

// Normal application settings keep their existing Windows registry location.
// Automated reviews explicitly select an INI file, avoiding real saved sessions.
inline QSettings appSettings() {
    const auto* application = QCoreApplication::instance();
    const QString directory = application
        ? application->property("teamsyncSettingsDir").toString() : QString();
    if (!directory.isEmpty())
        return QSettings(QDir(directory).filePath("TeamSync.ini"), QSettings::IniFormat);
    return QSettings("TeamSync", "TeamSync");
}

} // namespace teamsync
