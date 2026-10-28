#include "plugins/plugin_manager_dialog.h"
#include "plugins/plugin_package.h"
#include "core/i18n.h"
#include "core/ui_scale.h"
#include "core/ui_theme.h"
#include "ui/dialogs.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QVBoxLayout>

namespace jietuba::plugins {
namespace {
QString tr(const char *text) { return i18n_tr(text, "ClipboardWindow"); }

void style_dialog(QDialog &dialog) {
    const auto &t = get_ui_theme()->tokens();
    dialog.setStyleSheet(QString(R"(
        QDialog#pluginManagerDialog, QDialog#pluginSettingsDialog { background: %1; color: %2; }
        QListWidget#pluginList, QPlainTextEdit { background: %3; color: %2; border: 1px solid %4; border-radius: %5px; padding: %6px; }
        QListWidget#pluginList::item { padding: %7px; border-radius: %8px; }
        QListWidget#pluginList::item:selected { background: %9; color: %2; }
        QPushButton { background: %3; color: %2; border: 1px solid %4; border-radius: %8px; padding: %6px %10px; min-height: %11px; }
        QPushButton:hover { background: %9; }
        QPushButton:disabled { color: %12; }
    )").arg(t.window, t.text, t.input_background, t.border).arg(scaled(8)).arg(scaled(6)).arg(scaled(9)).arg(scaled(6))
              .arg(t.accent_soft).arg(scaled(14)).arg(scaled(24)).arg(t.text_disabled));
}

void edit_settings(QWidget *parent, const QString &id, const QJsonArray &settings, const QString &root) {
    QDialog dialog(parent);
    dialog.setObjectName("pluginSettingsDialog");
    style_dialog(dialog);
    dialog.setWindowTitle(tr("Plugin Settings"));
    dialog.resize(scaled(480), scaled(320));
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(scaled(16), scaled(16), scaled(16), scaled(16));
    layout->setSpacing(scaled(10));
    auto *scroll = new QScrollArea(&dialog);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    auto *fields = new QWidget;
    auto *fields_layout = new QVBoxLayout(fields);
    fields_layout->setContentsMargins(0, 0, 0, 0);
    fields_layout->setSpacing(scaled(8));
    scroll->setWidget(fields);
    layout->addWidget(scroll, 1);
    const auto current = read_plugin_config(id, root);
    QMap<QString, QPlainTextEdit *> editors;
    const QRegularExpression valid_key("^[a-z][a-z0-9_]{0,63}$");
    for (const auto &value : settings) {
        const auto spec = value.toObject();
        const QString key = spec.value("key").toString();
        if (!valid_key.match(key).hasMatch() || spec.value("type").toString() != "multiline" || editors.contains(key)) continue;
        const QString label = spec.value("label_i18n").toObject().value(I18nManager::get_current_language()).toString(spec.value("label").toString(key));
        auto *caption = new QLabel(label, &dialog);
        caption->setTextFormat(Qt::PlainText);
        auto *editor = new QPlainTextEdit(&dialog);
        editor->setObjectName("pluginSetting_" + key);
        editor->setPlainText(current.value(key).toString(spec.value("default").toString()));
        caption->setWordWrap(true);
        fields_layout->addWidget(caption);
        fields_layout->addWidget(editor, 1);
        editors.insert(key, editor);
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Save)->setText(tr("Save Changes"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        QJsonObject config = current;
        for (auto it = editors.cbegin(); it != editors.cend(); ++it) config.insert(it.key(), it.value()->toPlainText());
        if (!save_plugin_config(id, config, root)) { show_warning_dialog(&dialog, tr("Plugin Settings"), tr("Could not save plugin settings.")); return; }
        dialog.accept();
    });
    exec_dialog(&dialog, parent);
}
}

void install_plugin_interactively(QWidget *parent, const QString &plugin_root) {
    const QString path = get_open_file_name(parent, tr("Install Plugin"), {}, tr("Plugin Packages (*.jtbplugin)")).first;
    if (path.isEmpty()) return;
    if (!show_confirm_dialog(parent, tr("Install Plugin"), tr("Plugins can access screenshots, clipboard contents and files. Install only plugins you trust."))) return;
    const auto result = install_plugin_package(path, plugin_root);
    if (result.ok) show_info_dialog(parent, tr("Install Plugin"), tr("Plugin installed: %1").arg(result.id));
    else show_warning_dialog(parent, tr("Install Plugin"), tr(result.error.toUtf8().constData()));
}

void show_plugin_manager(QWidget *parent, const QString &plugin_root) {
    QDialog dialog(parent);
    dialog.setObjectName("pluginManagerDialog");
    style_dialog(dialog);
    dialog.setWindowTitle(tr("Manage Plugins"));
    dialog.resize(scaled(480), scaled(360));
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(scaled(16), scaled(16), scaled(16), scaled(16));
    layout->setSpacing(scaled(12));
    auto *list = new QListWidget(&dialog);
    list->setObjectName("pluginList");
    layout->addWidget(list);
    auto *buttons = new QHBoxLayout;
    auto *remove = new QPushButton(tr("Uninstall"), &dialog);
    remove->setObjectName("uninstallPlugin");
    auto *settings = new QPushButton(tr("Plugin Settings"), &dialog);
    settings->setObjectName("pluginSettings");
    auto *close = new QPushButton(tr("Close"), &dialog);
    buttons->addWidget(remove);
    buttons->addWidget(settings);
    buttons->addStretch();
    buttons->addWidget(close);
    layout->addLayout(buttons);
    auto refresh = [&] {
        list->blockSignals(true);
        list->clear();
        for (const auto &plugin : installed_plugins(plugin_root)) {
            const QString caption = plugin.version.isEmpty() ? plugin.name : plugin.name + "  " + plugin.version;
            auto *row = new QListWidgetItem(caption, list);
            row->setData(Qt::UserRole, plugin.id);
            row->setData(Qt::UserRole + 1, QJsonDocument(plugin.settings).toJson(QJsonDocument::Compact));
            row->setToolTip(plugin.id);
            row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
            row->setCheckState(plugin.enabled ? Qt::Checked : Qt::Unchecked);
        }
        list->blockSignals(false);
        remove->setEnabled(list->currentItem() != nullptr);
        settings->setEnabled(list->currentItem() && !QJsonDocument::fromJson(list->currentItem()->data(Qt::UserRole + 1).toByteArray()).array().isEmpty());
    };
    QObject::connect(list, &QListWidget::currentItemChanged, &dialog, [=](QListWidgetItem *item) {
        remove->setEnabled(item != nullptr);
        settings->setEnabled(item && !QJsonDocument::fromJson(item->data(Qt::UserRole + 1).toByteArray()).array().isEmpty());
    });
    QObject::connect(list, &QListWidget::itemChanged, &dialog, [&](QListWidgetItem *item) {
        if (!set_plugin_enabled(item->data(Qt::UserRole).toString(), item->checkState() == Qt::Checked, plugin_root)) {
            show_warning_dialog(&dialog, tr("Manage Plugins"), tr("Could not change plugin state."));
            refresh();
        }
    });
    QObject::connect(remove, &QPushButton::clicked, &dialog, [&] {
        auto *item = list->currentItem();
        if (!item || !show_confirm_dialog(&dialog, tr("Uninstall"), tr("Uninstall plugin %1?").arg(item->text()))) return;
        if (!uninstall_plugin(item->data(Qt::UserRole).toString(), plugin_root)) show_warning_dialog(&dialog, tr("Uninstall"), tr("Could not uninstall plugin."));
        refresh();
    });
    QObject::connect(settings, &QPushButton::clicked, &dialog, [&] {
        if (auto *item = list->currentItem()) edit_settings(&dialog, item->data(Qt::UserRole).toString(),
                                                           QJsonDocument::fromJson(item->data(Qt::UserRole + 1).toByteArray()).array(), plugin_root);
    });
    QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    refresh();
    exec_dialog(&dialog, parent);
}
}
