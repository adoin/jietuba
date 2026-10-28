#include "plugins/plugin_backup.h"
#include "plugins/plugin_package.h"
#include "clipboard/core/manager.h"
#include "clipboard/ui/windows/clipboard_window.h"
#include "clipboard/ui/widgets/group_bar.h"
#include "core/i18n.h"
#include "core/brand_config.h"
#include "core/ui_scale.h"
#include "settings/tool_settings.h"
#include "ui/dialogs.h"
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <optional>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace jietuba::plugins {
namespace {
QString tr(const char *text) { return i18n_tr(text, "ClipboardWindow"); }

std::pair<QString, QString> backup_executable(const QString &root) {
    const QString base = root.isEmpty() ? plugin_directory() : root;
    for (const auto &plugin : installed_plugins(base)) {
        if (!plugin.enabled || plugin.type != "extension") continue;
        const QString folder = QFileInfo(QDir(base).filePath(plugin.id)).canonicalFilePath();
        QFile manifest(QDir(folder).filePath("plugin.json"));
        if (!manifest.open(QIODevice::ReadOnly) || manifest.size() > 65536) continue;
        const auto spec = QJsonDocument::fromJson(manifest.readAll()).object();
        const QString command = spec.value("command").toString();
        const auto contributions = spec.value("contributes").toObject();
        const auto backup = contributions.value("clipboard").toObject().value("backup").toObject();
        const QString action = backup.value("command").toString();
        if (spec.value("api_version").toInt() != 2 || spec.value("type").toString() != "extension" || action.isEmpty() ||
            !contributions.value("commands").toArray().contains(action) ||
            command.isEmpty() || QFileInfo(command).isAbsolute()) continue;
        const QString program = QFileInfo(QDir(folder).filePath(command)).canonicalFilePath();
        if (!program.isEmpty() && program.startsWith(folder + '/', Qt::CaseInsensitive) && QFileInfo(program).isFile()) return {program, action};
    }
    return {};
}

std::optional<QString> ask_password(QWidget *parent, bool confirm) {
    QDialog dialog(parent);
    dialog.setObjectName("backupPasswordDialog");
    dialog.setWindowTitle(tr("Backup Password"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *password = new QLineEdit(&dialog);
    password->setObjectName("backupPassword");
    password->setEchoMode(QLineEdit::Password);
    password->setMaxLength(4096);
    form->addRow(tr("Password"), password);
    QLineEdit *repeat = nullptr;
    if (confirm) {
        repeat = new QLineEdit(&dialog);
        repeat->setObjectName("backupPasswordRepeat");
        repeat->setEchoMode(QLineEdit::Password);
        repeat->setMaxLength(4096);
        form->addRow(tr("Confirm Password"), repeat);
    }
    layout->addLayout(form);
    if (confirm) {
        auto *hint = new QLabel(tr("Keep this password; it cannot be recovered."), &dialog);
        hint->setWordWrap(true);
        layout->addWidget(hint);
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("OK"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (password->text().isEmpty() || (repeat && repeat->text() != password->text())) {
            show_warning_dialog(&dialog, tr("Backup Password"), tr("Enter a non-empty matching password."));
            return;
        }
        dialog.accept();
    });
    if (exec_dialog(&dialog, parent) != QDialog::Accepted) return std::nullopt;
    return password->text();
}
}

bool has_backup_plugin(const QString &root) { return !backup_executable(root).first.isEmpty(); }

BackupResult run_backup_plugin(const QJsonObject &request, QWidget *parent, const QString &root) {
    Q_UNUSED(parent);
    const auto [program, command] = backup_executable(root);
    if (program.isEmpty()) return {};
    const QJsonObject envelope{{"api_version", 2}, {"type", "extension"}, {"event", "command"}, {"command", command},
                               {"payload", QJsonObject{{"kind", "backup"}, {"data", request}}}, {"context", QJsonObject{}}};
    const QByteArray input = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
    if (input.size() > 65536) return {};
    QProcess process;
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
#endif
    process.setProgram(program);
    process.setWorkingDirectory(QFileInfo(program).absolutePath());
    QProgressDialog progress(tr("Working…"), tr("Cancel"), 0, 0, nullptr);
    progress.setWindowTitle(tr("Clipboard Backup"));
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(500);
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QByteArray output;
    bool cancelled = false, error = false;
    QObject::connect(&process, &QProcess::started, &loop, [&] { process.write(input); process.closeWriteChannel(); });
    QObject::connect(&process, &QProcess::readyReadStandardOutput, &loop, [&] {
        output += process.readAllStandardOutput();
        if (output.size() > 65536) { error = true; process.kill(); loop.quit(); }
    });
    QObject::connect(&process, &QProcess::readyReadStandardError, &loop, [&] { process.readAllStandardError(); });
    QObject::connect(&process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), &loop, &QEventLoop::quit);
    QObject::connect(&process, &QProcess::errorOccurred, &loop, [&](QProcess::ProcessError) { error = true; loop.quit(); });
    QObject::connect(&progress, &QProgressDialog::canceled, &loop, [&] { cancelled = true; process.kill(); loop.quit(); });
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&] { error = true; process.kill(); loop.quit(); });
    QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, &loop, [&] { cancelled = true; process.kill(); loop.quit(); });
    process.start();
    timeout.start(600000);
    if (!error && process.state() != QProcess::NotRunning) loop.exec();
    if (process.state() != QProcess::NotRunning) { process.kill(); process.waitForFinished(2000); }
    QObject::disconnect(&progress, nullptr, &loop, nullptr);
    progress.close();
    output += process.readAllStandardOutput();
    if (cancelled || error || output.size() > 65536 || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) return {false, cancelled, {}};
    const auto response = QJsonDocument::fromJson(output).object();
    if (response.value("api_version").toInt() != 2 || !response.value("ok").isBool()) return {};
    return {response.value("ok").toBool(), false, response.value("error").toString()};
}

void backup_clipboard(QWidget *parent, clipboard::ClipboardManager &manager) {
    const auto db = manager.get_db_path(), images = manager.get_images_dir();
    if (!db || !images) { show_warning_dialog(parent, tr("Clipboard Backup"), tr("Clipboard data is unavailable.")); return; }
    QString destination = get_save_file_name(parent, tr("Backup Clipboard"), {}, tr("Encrypted Backups (*.jtbbackup)")).first;
    if (destination.isEmpty()) return;
    if (QFileInfo(destination).suffix().isEmpty()) destination += ".jtbbackup";
    const QString source = QFileInfo(*db).canonicalFilePath();
    if (!source.isEmpty() && QFileInfo(destination).canonicalFilePath().compare(source, Qt::CaseInsensitive) == 0) {
        show_warning_dialog(parent, tr("Clipboard Backup"), tr("Choose a different backup file."));
        return;
    }
    const bool overwrite = QFileInfo::exists(destination);
    if (overwrite && !show_confirm_dialog(parent, tr("Backup Clipboard"), tr("Replace the existing backup file?"))) return;
    const auto password = ask_password(parent, true);
    if (!password) return;
    QTemporaryDir staging(QDir(QFileInfo(destination).absolutePath()).filePath(".jietuba-backup-XXXXXX"));
    if (!staging.isValid()) { show_warning_dialog(parent, tr("Clipboard Backup"), tr("Could not prepare the backup.")); return; }
    const QJsonObject request{{"operation", "export"}, {"db_path", *db},
                              {"images_dir", *images}, {"destination", destination}, {"staging", staging.path()}, {"password", *password}, {"overwrite", overwrite}};
    const auto result = run_backup_plugin(request, parent);
    if (result && QFileInfo(destination).isFile() && QFileInfo(destination).size() > 0)
        show_info_dialog(parent, tr("Clipboard Backup"), tr("Backup saved to %1").arg(QDir::toNativeSeparators(destination)));
    else if (!result.cancelled) {
        const QString message = result.error == "missing_image" ? tr("A referenced image is missing; the backup was not created.") :
                                result.error == "database_too_large" || result.error == "backup_too_large" || result.error == "too_many_files"
                                    ? tr("The clipboard history is too large for this backup plugin.") : tr("Backup failed or was cancelled.");
        show_warning_dialog(parent, tr("Clipboard Backup"), message);
    }
}

void restore_clipboard(QWidget *parent, clipboard::ClipboardManager &manager) {
    const QString source = get_open_file_name(parent, tr("Restore Clipboard"), {}, tr("Encrypted Backups (*.jtbbackup)")).first;
    if (source.isEmpty()) return;
    const QString folder = get_existing_directory(parent, tr("Choose a Folder for Restored Data"));
    if (folder.isEmpty()) return;
    const auto password = ask_password(parent, false);
    if (!password) return;
    QTemporaryDir staging(QDir(folder).filePath(".jietuba-restore-XXXXXX"));
    if (!staging.isValid()) { show_warning_dialog(parent, tr("Clipboard Backup"), tr("Could not create the restore folder.")); return; }
    const QJsonObject request{{"operation", "restore"}, {"source", source},
                              {"staging", staging.path()}, {"password", *password}};
    const auto result = run_backup_plugin(request, parent);
    if (!result) {
        if (!result.cancelled) show_warning_dialog(parent, tr("Clipboard Backup"), tr("Restore failed or was cancelled. Check the password and backup file."));
        return;
    }
    const QFileInfo restored_db(QDir(staging.path()).filePath("clipboard.db"));
    const QFileInfo restored_images(QDir(staging.path()).filePath("images"));
    if (!restored_db.isFile() || restored_db.isSymLink() || restored_db.size() == 0 ||
        restored_images.isSymLink() || restored_images.isJunction() || (restored_images.exists() && !restored_images.isDir())) {
        show_warning_dialog(parent, tr("Clipboard Backup"), tr("Restore failed or was cancelled. Check the password and backup file."));
        return;
    }
    const QString destination = QDir(folder).filePath(QStringLiteral(JTM_NAME "-restored-") + QUuid::createUuid().toString(QUuid::Id128).left(12));
    if (!QDir().rename(staging.path(), destination)) { show_warning_dialog(parent, tr("Clipboard Backup"), tr("Could not create the restore folder.")); return; }
    staging.setAutoRemove(false);
    if (!show_confirm_dialog(parent, tr("Clipboard Backup"), tr("Restored to %1. Use this history now? Current data will be kept.").arg(QDir::toNativeSeparators(destination)))) return;
    const QString previous = manager.get_db_path().value_or(QString());
    const QString restored = QDir(destination).filePath("clipboard.db");
    if (!manager.reset_storage(restored)) {
        if (!previous.isEmpty()) manager.reset_storage(previous);
        show_warning_dialog(parent, tr("Clipboard Backup"), tr("Could not open restored history; the current data was kept."));
        return;
    }
    get_tool_settings_manager()->set_clipboard_db_path(restored);
    for (auto *widget : QApplication::topLevelWidgets()) {
        if (auto *window = qobject_cast<clipboard::ClipboardWindow *>(widget)) {
            window->group_bar->switch_to_group(std::nullopt);
            window->group_bar->refresh_buttons();
            window->request_data_refresh("Clipboard restore");
        }
    }
}
}
