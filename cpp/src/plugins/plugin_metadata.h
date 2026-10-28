#pragma once
#include "clipboard/core/models.h"
#include <QList>
#include <QString>
class QWidget;
namespace jietuba::clipboard { class ClipboardManager; }
namespace jietuba::plugins {
struct MetadataPlugin { QString id, name; };
QList<MetadataPlugin> metadata_plugins(const clipboard::ClipboardItem &item, const QString &root = {});
bool edit_item_metadata(const QString &id, const clipboard::ClipboardItem &item, clipboard::ClipboardManager &manager,
                        QWidget *parent, const QString &root = {});
}
