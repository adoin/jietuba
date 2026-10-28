#pragma once
#include <QList>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
class QReadWriteLock;

namespace jietuba::plugins {
struct PluginInstallResult {
    bool ok = false;
    QString id;
    QString error;
};
struct InstalledPlugin {
    QString id, name, version, type;
    bool enabled = true;
    QJsonArray settings;
};

// Installs a .jtbplugin ZIP, staging it alongside the destination and restoring
// the old version if replacement fails. Never executes package contents.
PluginInstallResult install_plugin_package(const QString &package_path, const QString &plugin_root = {});
QString plugin_directory();
QReadWriteLock &plugin_lock();
QList<InstalledPlugin> installed_plugins(const QString &plugin_root = {});
bool set_plugin_enabled(const QString &id, bool enabled, const QString &plugin_root = {});
bool uninstall_plugin(const QString &id, const QString &plugin_root = {});
QString plugin_config_path(const QString &id, const QString &plugin_root = {});
QJsonObject read_plugin_config(const QString &id, const QString &plugin_root = {});
bool save_plugin_config(const QString &id, const QJsonObject &config, const QString &plugin_root = {});
}
