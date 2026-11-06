#pragma once
// SketchyUp's preferences file. On Linux it is Qt's native format, an INI file under
// $XDG_CONFIG_HOME (or ~/.config). macOS uses the same INI format in the same place rather than a
// CFPreferences plist: a plist is written through cfprefsd, which nothing in the environment
// redirects, while the INI file follows XDG_CONFIG_HOME and HOME on both systems.
#include <QCoreApplication>
#include <QSettings>

namespace sketchy {

#ifdef Q_OS_MACOS
inline constexpr QSettings::Format preferencesFormat = QSettings::IniFormat;
#else
inline constexpr QSettings::Format preferencesFormat = QSettings::NativeFormat;
#endif

// The user's SketchyUp preferences ("SketchyUp", "SketchyUp"), in preferencesFormat.
class Preferences : public QSettings {
  public:
    Preferences() : QSettings(preferencesFormat, QSettings::UserScope, "SketchyUp", "SketchyUp") {}
};

// The running application's preferences (QSettings()'s organization and name), in
// preferencesFormat.
class ApplicationPreferences : public QSettings {
  public:
    ApplicationPreferences()
        : QSettings(preferencesFormat, QSettings::UserScope, QCoreApplication::organizationName(),
                    QCoreApplication::applicationName()) {}
};

} // namespace sketchy
