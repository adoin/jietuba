#include "plugins/plugin_package.h"
#include "core/data_location.h"
#include "core/i18n.h"
#include <QtCore/private/qzipreader_p.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QReadWriteLock>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>

namespace jietuba::plugins {
namespace {
PluginInstallResult fail(const QString &message) { return {false, {}, message}; }

bool safe_path(const QString &path) {
    if (path.isEmpty() || path.size() > 180 || path.startsWith('/') || path.contains('\\') || path.contains(':')) return false;
    const QStringList segments = path.split('/', Qt::SkipEmptyParts);
    if (segments.isEmpty() || path.contains("//")) return false;
    static const QRegularExpression reserved("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\\..*)?$", QRegularExpression::CaseInsensitiveOption);
    for (const auto &segment : segments)
        if (segment == "." || segment == ".." || segment.endsWith('.') || segment.endsWith(' ') || reserved.match(segment).hasMatch()) return false;
    return true;
}

QString installed_path(const QString &root, const QString &id) {
    static const QRegularExpression valid_id("^[a-z0-9][a-z0-9._-]{0,63}$");
    if (!valid_id.match(id).hasMatch()) return {};
    const QFileInfo folder(QDir(root).filePath(id));
    if (folder.isSymLink() || folder.isJunction() || !folder.isDir()) return {};
    const QString base = QFileInfo(root).canonicalFilePath();
    const QString path = folder.canonicalFilePath();
    return !base.isEmpty() && path.startsWith(base + '/', Qt::CaseInsensitive) ? path : QString();
}
}

QString plugin_directory() { return QDir(data_location().dir).filePath("plugins"); }
QReadWriteLock &plugin_lock() { static QReadWriteLock lock; return lock; }

PluginInstallResult install_plugin_package(const QString &package_path, const QString &plugin_root) {
    QWriteLocker guard(&plugin_lock());
    const QFileInfo package(package_path);
    if (package.suffix().compare("jtbplugin", Qt::CaseInsensitive) != 0 || !package.isFile() || package.size() > 64 * 1024 * 1024)
        return fail("Invalid plugin package");
    QZipReader zip(package_path);
    if (!zip.exists() || !zip.isReadable()) return fail("Cannot read plugin package");
    const auto entries = zip.fileInfoList();
    if (entries.isEmpty() || entries.size() > 128) return fail("Invalid plugin package contents");
    QSet<QString> names;
    QSet<QString> regular_files;
    qint64 total = 0;
    bool has_manifest = false;
    for (const auto &entry : entries) {
        const QString path = entry.filePath;
        const QString normalized = path.endsWith('/') ? path.chopped(1) : path;
        const QString key = normalized.toCaseFolded();
        if (!safe_path(path) || !entry.isValid() || entry.isSymLink || names.contains(key) || entry.size < 0 || entry.size > 64 * 1024 * 1024)
            return fail("Unsafe plugin package contents");
        names.insert(key);
        if (entry.isFile && entry.size > 0) regular_files.insert(key);
        total += entry.size;
        if (total > 128 * 1024 * 1024) return fail("Plugin package is too large");
        if (path == "plugin.json" && entry.isFile && entry.size <= 65536) has_manifest = true;
    }
    if (!has_manifest) return fail("Plugin manifest is missing");
    const auto manifest = QJsonDocument::fromJson(zip.fileData("plugin.json"));
    if (!manifest.isObject()) return fail("Invalid plugin manifest");
    const auto object = manifest.object();
    const QString id = object.value("id").toString();
    const QString command = object.value("command").toString();
    static const QRegularExpression valid_id("^[a-z0-9][a-z0-9._-]{0,63}$");
    const bool valid_command = safe_path(command) && command.endsWith(".exe", Qt::CaseInsensitive) && regular_files.contains(command.toCaseFolded());
    if (!valid_id.match(id).hasMatch() || object.value("api_version").toInt() != 2 || object.value("type").toString() != "extension" ||
        (!command.isEmpty() && !valid_command))
        return fail("Unsupported plugin manifest");
    if (object.contains("settings")) {
        if (!object.value("settings").isArray() || object.value("settings").toArray().size() > 8) return fail("Unsupported plugin manifest");
        QSet<QString> keys;
        static const QRegularExpression valid_key("^[a-z][a-z0-9_]{0,63}$");
        for (const auto &value : object.value("settings").toArray()) {
            const auto setting = value.toObject();
            const QString key = setting.value("key").toString();
            if (!value.isObject() || setting.value("type").toString() != "multiline" || !valid_key.match(key).hasMatch() || keys.contains(key) ||
                !setting.value("label").isString() || setting.value("label").toString().size() > 160) return fail("Unsupported plugin manifest");
            keys.insert(key);
        }
    }
    {
        const auto contributions = object.value("contributes").toObject();
        if (!object.value("contributes").isObject()) return fail("Unsupported plugin manifest");
        static const QRegularExpression valid_command_id("^[a-z][a-z0-9_-]{0,63}$");
        static const QRegularExpression valid_event("^[a-z][a-z0-9_]*(\\.[a-z][a-z0-9_]*)+$");
        static const QRegularExpression valid_location("^[a-z][a-z0-9_.-]{0,63}$");
        const auto commands = contributions.value("commands").toArray();
        const auto hooks = contributions.value("hooks").toArray();
        const auto toolbar = contributions.value("toolbar").toArray();
        const auto clipboard = contributions.value("clipboard").toObject();
        if ((contributions.contains("commands") && !contributions.value("commands").isArray()) || commands.size() > 32 ||
            (contributions.contains("hooks") && !contributions.value("hooks").isArray()) || hooks.size() > 32 ||
            (contributions.contains("toolbar") && !contributions.value("toolbar").isArray()) || toolbar.size() > 16 ||
            (contributions.contains("clipboard") && !contributions.value("clipboard").isObject()) ||
            (commands.isEmpty() && hooks.isEmpty() && toolbar.isEmpty() && clipboard.isEmpty()) ||
            ((!commands.isEmpty() || !hooks.isEmpty()) && !valid_command)) return fail("Unsupported plugin manifest");
        QSet<QString> command_ids, hook_names, toolbar_ids;
        for (const auto &value : commands) {
            const QString key = value.toString();
            if (!value.isString() || !valid_command_id.match(key).hasMatch() || command_ids.contains(key)) return fail("Unsupported plugin manifest");
            command_ids.insert(key);
        }
        for (const auto &value : hooks) {
            const auto hook = value.toObject();
            const QString event = hook.value("event").toString();
            if (!value.isObject() || !valid_event.match(event).hasMatch() || event.size() > 96 || hook_names.contains(event) ||
                (hook.contains("priority") && (!hook.value("priority").isDouble() || hook.value("priority").toDouble() < -1000 || hook.value("priority").toDouble() > 1000))) return fail("Unsupported plugin manifest");
            hook_names.insert(event);
        }
        const auto valid_types = [](const QJsonValue &types) {
            if (!types.isArray() || types.toArray().isEmpty()) return false;
            for (const auto &kind : types.toArray())
                if (!kind.isString() || !QStringList{"text", "image", "file", "custom"}.contains(kind.toString())) return false;
            return true;
        };
        for (const auto &value : hooks) {
            const auto hook = value.toObject();
            if (hook.value("event").toString() == "clipboard.before_capture" && !valid_types(hook.value("content_types"))) return fail("Unsupported plugin manifest");
        }
        for (auto it = clipboard.begin(); it != clipboard.end(); ++it) {
            if (!QStringList{"preview", "action", "metadata", "backup"}.contains(it.key()) || !it.value().isObject()) return fail("Unsupported plugin manifest");
            const auto capability = it.value().toObject();
            if (it.key() != "backup" && !valid_types(capability.value("content_types"))) return fail("Unsupported plugin manifest");
            if (it.key() != "metadata" && !command_ids.contains(capability.value("command").toString())) return fail("Unsupported plugin manifest");
            if (capability.contains("formats")) {
                if (!capability.value("formats").isArray() || capability.value("formats").toArray().size() > 8) return fail("Unsupported plugin manifest");
                for (const auto &format : capability.value("formats").toArray())
                    if (!format.isString() || format.toString().isEmpty() || format.toString().size() > 128) return fail("Unsupported plugin manifest");
            }
            if (capability.contains("modes")) {
                if (!capability.value("modes").isArray() || capability.value("modes").toArray().isEmpty()) return fail("Unsupported plugin manifest");
                for (const auto &mode : capability.value("modes").toArray())
                    if (!mode.isString() || !QStringList{"preview", "formats"}.contains(mode.toString())) return fail("Unsupported plugin manifest");
            }
            if (it.key() == "metadata") {
                const auto fields = capability.value("fields").toArray();
                if (!capability.value("fields").isArray() || fields.isEmpty() || fields.size() > 8) return fail("Unsupported plugin manifest");
                QSet<QString> keys;
                static const QRegularExpression valid_key("^[a-z][a-z0-9_]{0,63}$");
                for (const auto &value : fields) {
                    const auto field = value.toObject();
                    const QString key = field.value("key").toString();
                    if (!value.isObject() || !QStringList{"line", "multiline"}.contains(field.value("type").toString()) ||
                        !valid_key.match(key).hasMatch() || keys.contains(key) || !field.value("label").isString() || field.value("label").toString().size() > 160)
                        return fail("Unsupported plugin manifest");
                    keys.insert(key);
                }
            }
        }
        for (const auto &value : toolbar) {
            const auto item = value.toObject();
            const QString key = item.value("id").toString();
            const QString icon = item.value("icon").toString();
            const auto pipeline = item.value("pipeline").toArray();
            if (!value.isObject() || !valid_command_id.match(key).hasMatch() || toolbar_ids.contains(key) ||
                !valid_location.match(item.value("location").toString()).hasMatch() || !item.value("name").isString() || item.value("name").toString().isEmpty() ||
                item.value("name").toString().size() > 160 || !item.value("pipeline").isArray() || pipeline.isEmpty() || pipeline.size() > 8 ||
                (!icon.isEmpty() && (!safe_path(icon) || !regular_files.contains(icon.toCaseFolded()) || (!icon.endsWith(".png", Qt::CaseInsensitive) && !icon.endsWith(".svg", Qt::CaseInsensitive))))) return fail("Unsupported plugin manifest");
            toolbar_ids.insert(key);
            for (const auto &step_value : pipeline) {
                const auto step = step_value.toObject();
                if (!step_value.isObject() || !valid_id.match(step.value("plugin").toString()).hasMatch() ||
                    !valid_command_id.match(step.value("command").toString()).hasMatch()) return fail("Unsupported plugin manifest");
            }
        }
    }
    if (names.contains(".disabled") || names.contains(".capture-filter")) return fail("Unsafe plugin package contents");
    const QString root = plugin_root.isEmpty() ? plugin_directory() : plugin_root;
    if (!QDir().mkpath(root)) return fail("Cannot create plugin directory");
    QTemporaryDir staging(QDir(root).filePath(".install-XXXXXX"));
    if (!staging.isValid()) return fail("Cannot prepare plugin installation");
    for (const auto &entry : entries) {
        const QString target = QDir(staging.path()).filePath(entry.filePath);
        if (entry.isDir) {
            if (!QDir().mkpath(target)) return fail("Cannot create plugin directory");
            continue;
        }
        if (!QDir().mkpath(QFileInfo(target).absolutePath())) return fail("Cannot create plugin directory");
        const QByteArray bytes = zip.fileData(entry.filePath);
        if (bytes.size() != entry.size || zip.status() != QZipReader::NoError) return fail("Cannot extract plugin package");
        QSaveFile file(target);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) return fail("Cannot extract plugin package");
    }
    bool is_capture_filter = false;
    for (const auto &value : object.value("contributes").toObject().value("hooks").toArray())
        if (value.toObject().value("event").toString() == "clipboard.before_capture") is_capture_filter = true;
    if (is_capture_filter) {
        QFile marker(QDir(staging.path()).filePath(".capture-filter"));
        if (!marker.open(QIODevice::WriteOnly)) return fail("Cannot prepare plugin installation");
    }
    const QString destination = QDir(root).filePath(id);
    const QFileInfo existing(destination);
    if (existing.isSymLink() || existing.isJunction() || (existing.exists() && !existing.isDir())) return fail("Cannot install plugin");
    QTemporaryDir backup(QDir(root).filePath(".backup-XXXXXX"));
    if (!backup.isValid()) return fail("Cannot prepare plugin installation");
    if (existing.isDir() && QFileInfo::exists(QDir(destination).filePath(".disabled"))) {
        QFile marker(QDir(staging.path()).filePath(".disabled"));
        if (!marker.open(QIODevice::WriteOnly)) return fail("Cannot prepare plugin installation");
    }
    const QString old_path = QDir(backup.path()).filePath("previous");
    const bool replacing = existing.exists();
    if (replacing && !QDir().rename(destination, old_path)) return fail("Cannot install plugin");
    if (!QDir().rename(staging.path(), destination)) {
        if (replacing && !QDir().rename(old_path, destination)) {
            backup.setAutoRemove(false);
            return fail("Cannot restore previous plugin: " + old_path);
        }
        return fail("Cannot install plugin");
    }
    staging.setAutoRemove(false);
    return {true, id, {}};
}

QList<InstalledPlugin> installed_plugins(const QString &plugin_root) {
    QReadLocker guard(&plugin_lock());
    const QString root = plugin_root.isEmpty() ? plugin_directory() : plugin_root;
    QList<InstalledPlugin> result;
    for (const auto &dir : QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name)) {
        const QString path = installed_path(root, dir.fileName());
        if (path.isEmpty()) continue;
        const bool enabled = !QFileInfo::exists(QDir(path).filePath(".disabled"));
        const bool was_filter = QFileInfo::exists(QDir(path).filePath(".capture-filter"));
        QFile file(QDir(path).filePath("plugin.json"));
        if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) {
            if (was_filter) result.append({dir.fileName(), dir.fileName(), {}, "extension", enabled, {}});
            continue;
        }
        const auto doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isObject() || doc.object().value("id").toString() != dir.fileName()) {
            if (was_filter) result.append({dir.fileName(), dir.fileName(), {}, "extension", enabled, {}});
            continue;
        }
        const auto data = doc.object();
        const QString name = data.value("name_i18n").toObject().value(I18nManager::get_current_language()).toString(data.value("name").toString(dir.fileName()));
        result.append({dir.fileName(), name, data.value("version").toString(),
                       data.value("type").toString(), enabled, data.value("settings").toArray()});
    }
    return result;
}

bool set_plugin_enabled(const QString &id, bool enabled, const QString &plugin_root) {
    QWriteLocker guard(&plugin_lock());
    const QString path = installed_path(plugin_root.isEmpty() ? plugin_directory() : plugin_root, id);
    if (path.isEmpty()) return false;
    const QString marker = QDir(path).filePath(".disabled");
    if (enabled) return !QFileInfo::exists(marker) || QFile::remove(marker);
    QSaveFile file(marker);
    return file.open(QIODevice::WriteOnly) && file.commit();
}

bool uninstall_plugin(const QString &id, const QString &plugin_root) {
    QWriteLocker guard(&plugin_lock());
    const QString path = installed_path(plugin_root.isEmpty() ? plugin_directory() : plugin_root, id);
    return !path.isEmpty() && QDir(path).removeRecursively();
}

QString plugin_config_path(const QString &id, const QString &plugin_root) {
    static const QRegularExpression valid_id("^[a-z0-9][a-z0-9._-]{0,63}$");
    if (!valid_id.match(id).hasMatch()) return {};
    return QDir(plugin_root.isEmpty() ? plugin_directory() : plugin_root).filePath(".data/" + id + ".json");
}

QJsonObject read_plugin_config(const QString &id, const QString &plugin_root) {
    QReadLocker guard(&plugin_lock());
    const QString path = plugin_config_path(id, plugin_root);
    if (path.isEmpty()) return {};
    const QFileInfo folder(QFileInfo(path).absolutePath());
    if (folder.isSymLink() || folder.isJunction() || QFileInfo(path).isSymLink()) return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool save_plugin_config(const QString &id, const QJsonObject &config, const QString &plugin_root) {
    QWriteLocker guard(&plugin_lock());
    const QString root = plugin_root.isEmpty() ? plugin_directory() : plugin_root;
    if (installed_path(root, id).isEmpty()) return false;
    const QString path = plugin_config_path(id, root);
    const QByteArray data = QJsonDocument(config).toJson(QJsonDocument::Compact);
    if (path.isEmpty() || data.size() > 65536) return false;
    const QString directory = QFileInfo(path).absolutePath();
    const QFileInfo before(directory);
    if (before.isSymLink() || before.isJunction() || !QDir().mkpath(directory)) return false;
    const QFileInfo after(directory);
    if (after.isSymLink() || after.isJunction() || QFileInfo(path).isSymLink()) return false;
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}
}
