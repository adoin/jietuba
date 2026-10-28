#include "core/data_location.h"
#include "core/constants.h"
#include "core/brand_config.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QTemporaryDir>
#include <QDebug>
#include <iterator>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace jietuba {
namespace {
const QString SETTINGS_FILE_NAME = "settings.ini";

bool copy_tree(const QString &source, const QString &destination) {
    const QFileInfo info(source);
    if (info.isSymLink() || info.isJunction()) return false;
    if (info.isFile()) return QFile::copy(source, destination);
    if (!info.isDir() || !QDir(source).isReadable() || !QDir().mkpath(destination)) return false;
    const auto entries = QDir(source).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const auto &entry : entries)
        if (!copy_tree(entry.absoluteFilePath(), QDir(destination).filePath(entry.fileName()))) return false;
    return true;
}

// Settings are read before QApplication exists, when applicationFilePath() is still empty.
QString program_file() {
#ifdef Q_OS_WIN
    wchar_t path[MAX_PATH * 4] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    return QDir::fromNativeSeparators(QString::fromWCharArray(path, static_cast<int>(length)));
#else
    return QCoreApplication::applicationFilePath();
#endif
}
}

QString DataLocation::settings_file() const { return QDir(dir).filePath(SETTINGS_FILE_NAME); }

DataLocation resolve_data_location(const QString &program_dir, const QString &roaming_dir) {
    if (!program_dir.isEmpty() && QFileInfo(QDir(program_dir).filePath(SETTINGS_FILE_NAME)).isFile())
        return {QDir::cleanPath(QDir::fromNativeSeparators(program_dir)), true};
    return {QDir(QDir::fromNativeSeparators(roaming_dir)).filePath(QStringLiteral(JTM_DISPLAY_NAME)), false};
}

bool migrate_legacy_data(const QString &roaming_dir) {
    const QDir base(QDir::fromNativeSeparators(roaming_dir));
    const QString old_path = base.filePath("Jietuba");
    const QString new_path = base.filePath(QStringLiteral(JTM_DISPLAY_NAME));
    if (old_path.compare(new_path, Qt::CaseInsensitive) == 0 || !QFileInfo::exists(old_path)) return true;
    if (!QDir().mkpath(base.path())) return false;
    QLockFile lock(base.filePath(QStringLiteral(".%1-migration.lock").arg(QStringLiteral(JTM_NAME))));
    if (!lock.tryLock(10000)) return false;
    if (QFileInfo::exists(new_path)) {
        const QFileInfo existing(new_path);
        return existing.isDir() && !existing.isSymLink() && !existing.isJunction();
    }
    const QFileInfo legacy(old_path);
    if (!legacy.isDir() || !QDir(old_path).isReadable() || legacy.isSymLink() || legacy.isJunction()) return false;
    QTemporaryDir staging(base.filePath(QStringLiteral(".%1-migration-XXXXXX").arg(QStringLiteral(JTM_NAME))));
    if (!staging.isValid()) return false;
    const auto entries = QDir(old_path).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const auto &entry : entries)
        if (!copy_tree(entry.absoluteFilePath(), QDir(staging.path()).filePath(entry.fileName()))) return false;
    if (!QDir().rename(staging.path(), new_path)) return false;
    staging.setAutoRemove(false);
    return true;
}

const DataLocation &data_location() {
    static const DataLocation location = [] {
        const QString roaming = roaming_app_data_dir();
        auto selected = resolve_data_location(test_sandbox_dir().isEmpty() ? QFileInfo(program_file()).absolutePath() : QString(), roaming);
        if (!selected.beside_program && !migrate_legacy_data(roaming)) {
            qWarning() << "Could not migrate user data; continuing with the existing data directory.";
            selected.dir = QDir(roaming).filePath("Jietuba");
        }
        return selected;
    }();
    return location;
}

QString roaming_app_data_dir() {
    const QString base = qEnvironmentVariable("APPDATA");
    return base.isEmpty() ? QDir(QDir::homePath()).filePath("AppData/Roaming") : QDir::fromNativeSeparators(base);
}
}
