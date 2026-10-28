#pragma once
#include <QString>

namespace jietuba {
// Where the settings file lives: the settings.ini beside the program when there is one, else
// %APPDATA%\<brand>\settings.ini. Settings bound to this computer stay in the registry
// (SettingsStore), and the clipboard history defaults to %APPDATA%\<brand>\clipboard
// (ClipboardManager), so neither moves with the file. Logs and caches stay in
// %LOCALAPPDATA%\<brand> (get_app_data_dir).
struct DataLocation {
    QString dir;
    bool beside_program = false;
    QString settings_file() const;
};
// The clipboard window's geometry, a group of the settings file.
inline const QString CLIPBOARD_WINDOW_SETTINGS_GROUP = "clipboard_window";

DataLocation resolve_data_location(const QString &program_dir, const QString &roaming_dir);
// This process's, fixed by the first call. Inside the test sandbox it is never the test program's folder.
const DataLocation &data_location();
// %APPDATA%.
QString roaming_app_data_dir();
}
