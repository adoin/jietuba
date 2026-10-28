#pragma once
#include <QString>
class QWidget;
namespace jietuba::plugins {
void install_plugin_interactively(QWidget *parent, const QString &plugin_root = {});
void show_plugin_manager(QWidget *parent, const QString &plugin_root = {});
}
