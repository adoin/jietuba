#include "plugins/plugin_metadata.h"
#include "plugins/plugin_package.h"
#include "clipboard/core/manager.h"
#include "core/i18n.h"
#include "core/ui_scale.h"
#include "ui/dialogs.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QVBoxLayout>
#include <algorithm>

namespace jietuba::plugins {
namespace {
QString tr(const char *text) { return i18n_tr(text, "ClipboardWindow"); }
struct Spec { QString id, name; QJsonArray fields; };
QList<Spec> metadata_specs(const clipboard::ClipboardItem &item, const QString &root) {
    QList<Spec> result;
    for (const auto &dir : QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name)) {
        if (dir.isJunction() || QFileInfo::exists(QDir(dir.absoluteFilePath()).filePath(".disabled"))) continue;
        QFile file(QDir(dir.absoluteFilePath()).filePath("plugin.json"));
        if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) continue;
        const auto spec = QJsonDocument::fromJson(file.readAll()).object();
        const auto capability = spec.value("contributes").toObject().value("clipboard").toObject().value("metadata").toObject();
        const QString id = spec.value("id").toString();
        if (id != dir.fileName() || spec.value("api_version").toInt() != 2 || spec.value("type").toString() != "extension" ||
            !capability.value("content_types").toArray().contains(item.content_type)) continue;
        const QString name = spec.value("name_i18n").toObject().value(I18nManager::get_current_language()).toString(spec.value("name").toString(id));
        result.append({id, name, capability.value("fields").toArray()});
    }
    return result;
}
}

QList<MetadataPlugin> metadata_plugins(const clipboard::ClipboardItem &item, const QString &root) {
    QList<MetadataPlugin> result;
    for (const auto &spec : metadata_specs(item, root.isEmpty() ? plugin_directory() : root)) result.append({spec.id, spec.name});
    return result;
}

bool edit_item_metadata(const QString &id, const clipboard::ClipboardItem &item, clipboard::ClipboardManager &manager,
                        QWidget *parent, const QString &root) {
    const auto specs = metadata_specs(item, root.isEmpty() ? plugin_directory() : root);
    const auto found = std::find_if(specs.begin(), specs.end(), [&id](const auto &spec) { return spec.id == id; });
    if (found == specs.end()) return false;
    QDialog dialog(parent);
    dialog.setObjectName("pluginMetadataDialog");
    dialog.setWindowTitle(found->name);
    dialog.resize(scaled(480), scaled(360));
    auto *layout = new QVBoxLayout(&dialog);
    auto *scroll = new QScrollArea(&dialog);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    auto *fields = new QWidget;
    auto *fields_layout = new QVBoxLayout(fields);
    scroll->setWidget(fields);
    layout->addWidget(scroll, 1);
    const QJsonObject existing = manager.plugin_metadata(item.id, id);
    QMap<QString, QWidget *> editors;
    const QRegularExpression valid_key("^[a-z][a-z0-9_]{0,63}$");
    for (const auto &value : found->fields) {
        const auto field = value.toObject();
        const QString key = field.value("key").toString();
        const QString type = field.value("type").toString();
        if (!valid_key.match(key).hasMatch() || editors.contains(key) || !QStringList{"line", "multiline"}.contains(type)) continue;
        const QString label = field.value("label_i18n").toObject().value(I18nManager::get_current_language()).toString(field.value("label").toString(key));
        auto *caption = new QLabel(label, fields);
        caption->setTextFormat(Qt::PlainText);
        caption->setWordWrap(true);
        fields_layout->addWidget(caption);
        if (type == "line") {
            auto *editor = new QLineEdit(existing.value(key).toString(), fields);
            editor->setObjectName("metadata_" + key);
            editor->setMaxLength(4096);
            fields_layout->addWidget(editor);
            editors.insert(key, editor);
        } else {
            auto *editor = new QPlainTextEdit(fields);
            editor->setObjectName("metadata_" + key);
            editor->setPlainText(existing.value(key).toString());
            fields_layout->addWidget(editor, 1);
            editors.insert(key, editor);
        }
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Save)->setText(tr("Save Changes"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    layout->addWidget(buttons);
    bool saved = false;
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        QJsonObject data = existing;
        for (auto it = editors.cbegin(); it != editors.cend(); ++it) {
            const auto *line = qobject_cast<QLineEdit *>(it.value());
            const auto *multiline = qobject_cast<QPlainTextEdit *>(it.value());
            const QString text = line ? line->text() : multiline ? multiline->toPlainText() : QString();
            if (text.trimmed().isEmpty()) data.remove(it.key());
            else data.insert(it.key(), text);
        }
        if (!manager.set_plugin_metadata(item.id, id, data)) { show_warning_dialog(&dialog, found->name, tr("Could not save plugin data.")); return; }
        saved = true;
        dialog.accept();
    });
    exec_dialog(&dialog, parent);
    return saved;
}
}
