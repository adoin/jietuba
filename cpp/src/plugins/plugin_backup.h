#pragma once
#include <QJsonObject>
#include <QString>
class QWidget;
namespace jietuba::clipboard { class ClipboardManager; }
namespace jietuba::plugins {
struct BackupResult {
    bool ok = false;
    bool cancelled = false;
    QString error;
    explicit operator bool() const { return ok; }
};
bool has_backup_plugin(const QString &root = {});
// Runs the selected installed backup provider. Passwords travel only over stdin.
BackupResult run_backup_plugin(const QJsonObject &request, QWidget *parent, const QString &root = {});
void backup_clipboard(QWidget *parent, clipboard::ClipboardManager &manager);
void restore_clipboard(QWidget *parent, clipboard::ClipboardManager &manager);
}
