#include "clipboard/ui/widgets/preview_popup.h"
#include "clipboard/core/manager.h"
#include "clipboard/preview/preview_provider.h"
#include "core/i18n.h"
#include "core/logger.h"
#include "core/text_utils.h"
#include "core/ui_scale.h"
#include "core/ui_theme.h"
#include "ui/fluent_lite/inputs.h"
#include <QApplication>
#include <QCoreApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>
#include <QPainter>
#include <QScreen>
#include <QScrollArea>
#include <QStringDecoder>
#include <QTextDocument>

namespace jietuba::clipboard {
namespace {
QString tr_preview(const char *text) { return QCoreApplication::translate("ClipboardPreview", text); }

QPixmap preview_pixmap(const QByteArray &data) {
    QPixmap pixmap;
    pixmap.loadFromData(data);
    return pixmap.scaled(scaled(400), scaled(300), Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

// Clipboard HTML is untrusted. Do not let QTextDocument read local files or
// fetch remote resources while displaying a preview.
class PreviewDocument : public QTextDocument {
public:
    explicit PreviewDocument(QObject *parent) : QTextDocument(parent) {}
protected:
    QVariant loadResource(int, const QUrl &) override { return {}; }
};

bool local_file_preview(const QString &path, QString *text, QPixmap *image) {
    if (path.startsWith("\\\\") || path.startsWith("//")) return false;
    const QFileInfo info(path);
    if (!info.isFile()) return false;
    const QString suffix = info.suffix().toLower();
    const QStringList image_types = {"png", "jpg", "jpeg", "bmp", "gif", "webp"};
    if (image_types.contains(suffix)) {
        QImageReader reader(path);
        const QSize size = reader.size();
        if (!size.isValid() || qint64(size.width()) * size.height() > 40000000) return false;
        *image = QPixmap::fromImage(reader.read());
        return !image->isNull();
    }
    const QStringList text_types = {"txt", "md", "log", "csv", "tsv", "json", "xml", "html", "htm", "css", "js", "ts", "py", "cpp", "h", "rs"};
    if (!text_types.contains(suffix) || info.size() > 1024 * 1024) return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QByteArray bytes = file.read(1024 * 1024 + 1);
    if (bytes.size() > 1024 * 1024 || bytes.contains('\0')) return false;
    QStringDecoder decoder(QStringDecoder::Utf8);
    *text = decoder.decode(bytes);
    return !decoder.hasError();
}

}

QDialog *show_detail_preview(const ClipboardItem &item, ClipboardManager &manager, QWidget *parent, const Theme &theme, const QString &mode) {
    auto *dialog = new QDialog(parent, Qt::Window);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setObjectName("clipboardDetailPreview");
    dialog->setWindowTitle(i18n_tr(mode == "formats" ? "Formats" : "Preview", "ClipboardWindow"));
    dialog->resize(scaled(700), scaled(500));
    const auto &c = theme.colors;
    dialog->setStyleSheet(QString("QDialog { background: %1; color: %2; } QTextEdit, QScrollArea { background: %1; color: %2; border: 1px solid %3; border-radius: %4px; }")
                              .arg(c.bg_primary, c.text_primary, c.border_primary).arg(scaled(6)));
    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(scaled(12), scaled(12), scaled(12), scaled(12));
    if (mode == "formats") {
        auto *selector = new QComboBox(dialog);
        selector->setObjectName("previewFormatSelector");
        selector->addItem(i18n_tr("All", "ClipboardWindow"), QString());
        for (const auto &name : manager.format_names(item.id)) selector->addItem(name, name);
        layout->addWidget(selector);
        auto *editor = new TextEdit(dialog);
        editor->setObjectName("previewContent");
        editor->setReadOnly(true);
        editor->setDocument(new PreviewDocument(editor));
        layout->addWidget(editor);
        auto refresh = [editor, selector, item, &manager] {
            const QString selected = selector->currentData().toString();
            const auto rendered = resolve_preview(item, manager, {}, "formats", selected);
            if (rendered && rendered->kind == PreviewContent::Kind::Html) editor->setHtml(rendered->text);
            else if (rendered && rendered->kind == PreviewContent::Kind::Markdown) editor->document()->setMarkdown(rendered->text);
            else if (rendered && rendered->kind == PreviewContent::Kind::Text) editor->setPlainText(rendered->text);
            else if (!selected.isEmpty()) {
                const auto bytes = manager.get_format(item.id, selected);
                editor->setPlainText(bytes ? QString::fromLatin1(bytes->left(256).toHex(' ')) + (bytes->size() > 256 ? QString::fromUtf8(" …") : QString())
                                          : i18n_tr("No stored formats", "ClipboardWindow"));
            } else {
                const QString names = manager.format_names(item.id).join('\n');
                editor->setPlainText(names.isEmpty() ? i18n_tr("No stored formats", "ClipboardWindow") : names);
            }
        };
        QObject::connect(selector, &QComboBox::currentIndexChanged, dialog, [refresh](int) { refresh(); });
        refresh();
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
        return dialog;
    }
    const auto preview = resolve_preview(item, manager, {}, mode);
    QPixmap image;
    QString text = item.content_type == "custom" ? item.display_text() : item.content;
    const bool rich = item.content_type == "text" && item.html_content && !item.html_content->isEmpty();
    QString file_path;
    if (preview) {
        text = preview->text;
        if (preview->kind == PreviewContent::Kind::Image) image.loadFromData(preview->image);
    } else if (item.content_type == "image") {
        if (item.image_id) if (const auto bytes = manager.get_image_data(*item.image_id)) image.loadFromData(*bytes);
        if (image.isNull() && item.thumbnail && item.thumbnail->startsWith("data:image"))
            image.loadFromData(QByteArray::fromBase64(item.thumbnail->mid(item.thumbnail->indexOf(u',') + 1).toLatin1()));
    } else if (item.content_type == "file") {
        const QStringList paths = item_paths(item);
        text = paths.join('\n');
        if (paths.size() == 1) {
            file_path = paths.front();
            QString file_text;
            if (local_file_preview(file_path, &file_text, &image) && image.isNull()) text = file_text;
        }
    }
    if (!file_path.isEmpty()) {
        auto *label = new QLabel(QDir::toNativeSeparators(file_path), dialog);
        label->setTextFormat(Qt::PlainText);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(label);
    }
    if (!image.isNull()) {
        auto *scroll = new QScrollArea(dialog);
        scroll->setObjectName("previewImageScroll");
        auto *label = new QLabel;
        label->setPixmap(image);
        label->setAlignment(Qt::AlignCenter);
        label->adjustSize();
        scroll->setWidget(label);
        scroll->setAlignment(Qt::AlignCenter);
        layout->addWidget(scroll);
    } else {
        auto *editor = new TextEdit(dialog);
        editor->setObjectName("previewContent");
        editor->setReadOnly(true);
        editor->setDocument(new PreviewDocument(editor));
        if (preview && preview->kind == PreviewContent::Kind::Html) editor->setHtml(text);
        else if (preview && preview->kind == PreviewContent::Kind::Markdown) editor->document()->setMarkdown(text);
        else if (!preview && mode != "formats" && rich) editor->setHtml(*item.html_content);
        else editor->setPlainText(text);
        layout->addWidget(editor);
    }
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
    return dialog;
}

QPoint side_position(const QSize &size, const QPoint &pos, const std::optional<QRect> &avoid_rect, const QString &prefer_side, std::optional<int> gap_value) {
    const int gap = gap_value ? *gap_value : scaled(10);
    QScreen *screen = QGuiApplication::screenAt(pos);
    if (!screen) screen = QGuiApplication::primaryScreen();
    const QRect screen_geo = screen->availableGeometry();
    const int width = size.width(), height = size.height();

    int x = pos.x() + scaled(20);
    int y = pos.y();

    if (avoid_rect) {
        const int left_x = avoid_rect->left() - width - gap;
        const int right_x = avoid_rect->right() + gap;
        const bool left_fits = avoid_rect->left() - screen_geo.left() >= width + gap;
        const bool right_fits = screen_geo.right() - avoid_rect->right() >= width + gap;
        const int roomier_x = screen_geo.right() - avoid_rect->right() >= avoid_rect->left() - screen_geo.left() ? right_x : left_x;
        if (prefer_side == "left") x = left_fits ? left_x : right_fits ? right_x : roomier_x;
        else x = right_fits ? right_x : left_fits ? left_x : roomier_x;
    } else if (x + width > screen_geo.right()) {
        // The popup must not cover the cursor: the list would get Leave and close it, then reopen it when the mouse returns, flickering.
        x = pos.x() - scaled(20) - width;
    }

    if (x + width > screen_geo.right()) x = screen_geo.right() - width - gap;
    if (x < screen_geo.left()) x = screen_geo.left();
    if (y + height > screen_geo.bottom()) y = screen_geo.bottom() - height - gap;
    if (y < screen_geo.top()) y = screen_geo.top();
    return {x, y};
}

PreviewPopup *PreviewPopup::instance() {
    static PreviewPopup *popup = new PreviewPopup;
    return popup;
}

PreviewPopup::PreviewPopup() : QWidget(nullptr) {
    // Frameless, tool window, on top.
    setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setup_ui();
    show_timer_.setSingleShot(true);
    connect(&show_timer_, &QTimer::timeout, this, [this] { do_show(); });
}

void PreviewPopup::setup_ui() {
    layout_ = new QVBoxLayout(this);
    layout_->setSpacing(0);

    // Title row (text previews only; hidden for images).
    title_label = new QLabel;
    title_label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    title_label->hide();
    layout_->addWidget(title_label);

    color_label = new QLabel;
    color_label->hide();
    layout_->addWidget(color_label);

    content_widget = new TextEdit;
    content_widget->setReadOnly(true);
    content_widget->setLineWrapMode(QTextEdit::WidgetWidth);
    content_widget->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    content_widget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout_->addWidget(content_widget);

    image_label = new QLabel;
    image_label->setAlignment(Qt::AlignCenter);
    image_label->hide();
    layout_->addWidget(image_label);
    apply_scale();
}

void PreviewPopup::apply_scale() {
    // The type selector names the C++ class (namespaces become "--").
    setStyleSheet(QString(R"(
            jietuba--clipboard--PreviewPopup {
                background: #FAFAFA;
                border: 1px solid #D0D0D0;
                border-radius: %1px;
            }
        )")
                      .arg(scaled(8)));
    const int margin = scaled(2);
    layout_->setContentsMargins(margin, margin, margin, margin);
    title_label->setStyleSheet(QString("font-size: %1px; color: #666; font-weight: bold; padding-left: %2px;").arg(scaled(12)).arg(scaled(4)));
    content_widget->setStyleSheet(QString(R"(
            QTextEdit {
                background: #FAFAFA;
                border: 1px solid #E0E0E0;
                border-radius: %1px;
                padding: %1px %2px;
                font-size: %3px;
                color: #333;
            }
        )")
                                      .arg(scaled(4))
                                      .arg(scaled(8))
                                      .arg(scaled(13)));
    content_widget->document()->setDocumentMargin(scaled(2));
    // Only a maximum size, so the content sizes itself.
    content_widget->setMaximumSize(scaled(500), scaled(400));
    set_own_style(image_label, QString("background: #F0F0F0; border: 1px solid #E0E0E0; border-radius: %1px;").arg(scaled(4)));
}

void PreviewPopup::set_display_enabled(bool enabled) {
    display_enabled_ = enabled;
    if (!display_enabled_) force_cleanup();
}

void PreviewPopup::show_preview(const std::optional<ClipboardItem> &item, const QPoint &pos, int delay_ms, const QString &prefer_side,
                                const std::optional<QRect> &avoid_rect) {
    if (!display_enabled_) return;
    if (!item) {
        hide_preview();
        return;
    }
    // The same item already shown: nothing to do.
    if (current_item_id_ == item->id && isVisible()) return;
    pending_item_ = item;
    pending_pos_ = pos;
    pending_prefer_side_ = prefer_side;
    pending_avoid_rect_ = avoid_rect;
    if (delay_ms > 0) show_timer_.start(delay_ms);
    else do_show();
}

void PreviewPopup::do_show() {
    // The window may hide before the timer fires; an old delayed show must not bring the preview back after cleanup.
    if (!display_enabled_) {
        force_cleanup();
        return;
    }
    if (!pending_item_) return;
    const ClipboardItem item = *pending_item_;
    current_item_id_ = item.id;
    color_label->hide();

    if (item.content_type == "image") {
        // Images the backend could not decode (several pictures copied from Word, EMF) keep no pixels;
        // their text, such as "[PNG+CF_DIB 7.9 MB]", is shown instead of an empty frame.
        if (!show_image_preview(item)) show_text_preview(item);
    } else if (item.content_type == "file") show_file_preview(item);
    else if (item.content_type == "text") show_text_preview(item);
    else if (item.content_type == "custom") {
        ClipboardItem summary = item;
        summary.content = item.display_text();
        show_text_preview(summary);
    }
    else return;

    adjustSize();
    move(side_position(size(), pending_pos_, pending_avoid_rect_, pending_prefer_side_.isEmpty() ? QString("auto") : pending_prefer_side_));
    show();
}

void PreviewPopup::show_text_preview(const ClipboardItem &item) {
    // The time above, the full content below.
    title_label->setText(item.created_at.isValid() ? item.created_at.toString("yyyy-MM-dd HH:mm:ss") : tr_preview("📝 文本"));
    title_label->show();
    // Length limited to stay responsive.
    const QString content = left_chars(item.content, 2000);
    content_widget->setMinimumSize(0, 0);
    content_widget->setMaximumSize(scaled(500), scaled(400));
    content_widget->setPlainText(content);
    adjust_content_size();
    content_widget->show();
    image_label->hide();
    if (item.color) {
        const QSize swatch_size(scaled(240), scaled(128));
        QPixmap swatch(swatch_size);
        QPainter painter(&swatch);
        const int tile = scaled(12);
        for (int y = 0; y < swatch.height(); y += tile)
            for (int x = 0; x < swatch.width(); x += tile)
                painter.fillRect(x, y, tile, tile, (x / tile + y / tile) % 2 ? QColor("#D8D8D8") : QColor(Qt::white));
        painter.fillRect(swatch.rect(), *item.color);
        painter.setPen(QColor("#999999"));
        painter.drawRect(swatch.rect().adjusted(0, 0, -1, -1));
        painter.end();
        color_label->setFixedSize(swatch_size);
        color_label->setPixmap(swatch);
        color_label->show();
        content_widget->setFixedWidth(swatch_size.width());
    }
}

void PreviewPopup::show_file_preview(const ClipboardItem &item) {
    title_label->hide();
    image_label->hide();

    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(item.content.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        log_exception(error.error != QJsonParseError::NoError ? error.errorString() : "content is not a JSON object", T("加载文本预览"));
        content_widget->setMinimumSize(0, 0);
        content_widget->setMaximumSize(scaled(500), scaled(400));
        content_widget->setPlainText(item.content);
        adjust_content_size();
        content_widget->show();
        return;
    }
    QStringList files;
    for (const auto &value : document.object().value("files").toArray()) files.append(value.toString());
    if (files.isEmpty()) {
        content_widget->setPlainText(tr_preview("无文件信息"));
        content_widget->show();
        return;
    }

    QStringList lines;
    if (files.size() == 1) {
        lines << QFileInfo(files[0]).fileName() << "" << files[0];
    } else {
        // Grouped by folder, in order of first appearance.
        QList<std::pair<QString, QStringList>> dir_files;
        for (const auto &filepath : files) {
            const QFileInfo info(filepath);
            const QString dir_path = QDir::toNativeSeparators(info.path());
            const QString filename = info.fileName();
            auto it = std::find_if(dir_files.begin(), dir_files.end(), [&dir_path](const auto &entry) { return entry.first == dir_path; });
            if (it == dir_files.end()) dir_files.append({dir_path, {filename}});
            else it->second.append(filename);
        }
        if (dir_files.size() == 1) {
            for (const auto &fn : dir_files.front().second) lines.append(fn);
            lines << "" << dir_files.front().first;
        } else {
            int group_num = 1;
            for (const auto &[dir_path, filenames] : dir_files) {
                if (filenames.size() == 1) {
                    lines.append(QString("[%1] %2").arg(group_num).arg(filenames.front()));
                    lines.append(QDir::toNativeSeparators(QDir(dir_path).filePath(filenames.front())));
                } else {
                    lines.append(QString("[%1]").arg(group_num));
                    for (const auto &fn : filenames) lines.append("  " + fn);
                    lines.append(dir_path);
                }
                group_num += 1;
                lines.append("");
            }
            if (!lines.isEmpty() && lines.back().isEmpty()) lines.removeLast();
        }
    }
    content_widget->setMinimumSize(0, 0);
    content_widget->setMaximumSize(scaled(500), scaled(400));
    content_widget->setPlainText(lines.join("\n"));
    adjust_content_size();
    content_widget->show();
}

void PreviewPopup::adjust_content_size() {
    // Sized to the content: the ideal width when it fits unwrapped, else the maximum width with the document wrapping.
    QTextDocument *doc = content_widget->document();
    const QFontMetrics font_metrics = content_widget->fontMetrics();
    const int line_height = font_metrics.height();
    const QStringList lines = doc->toPlainText().split(u'\n');
    int max_line_width = 0;
    for (const auto &line : lines) max_line_width = std::max(max_line_width, font_metrics.horizontalAdvance(line));

    // CSS padding 4px vertical + 8px horizontal, border 2px, document margin 2px (values at 100%).
    const int h_padding = scaled(8) * 2 + scaled(2) * 2 + scaled(4);
    const int v_padding = scaled(4) * 2 + scaled(2) * 2 + scaled(4);
    const int ideal_width = max_line_width + h_padding;
    const int max_width = scaled(500);
    const int min_width = scaled(80);

    int actual_width = 0, actual_height = 0;
    if (ideal_width <= max_width) {
        actual_width = std::max(ideal_width, min_width);
        doc->setTextWidth(-1);
        actual_height = static_cast<int>(lines.size()) * line_height + v_padding;
    } else {
        actual_width = max_width;
        doc->setTextWidth(max_width - h_padding);
        actual_height = static_cast<int>(doc->size().height()) + v_padding;
    }
    const int max_height = scaled(550);
    const int min_height = line_height + v_padding;
    actual_height = std::max(std::min(actual_height, max_height), min_height);
    content_widget->setFixedSize(actual_width, actual_height);
}

bool PreviewPopup::show_image_preview(const ClipboardItem &item) {
    title_label->hide();
    content_widget->hide();
    // A label left visible would show the previous record's picture.
    image_label->hide();

    QPixmap pixmap;
    // The full image first.
    if (manager_ && item.image_id && !item.image_id->isEmpty()) {
        const auto image_data = manager_->get_image_data(*item.image_id);
        if (image_data && !image_data->isEmpty()) pixmap = preview_pixmap(*image_data);
    }
    // Else the thumbnail.
    if (pixmap.isNull() && item.thumbnail && item.thumbnail->startsWith("data:image")) {
        const QByteArray encoded = item.thumbnail->mid(item.thumbnail->indexOf(u',') + 1).toLatin1();
        pixmap = preview_pixmap(QByteArray::fromBase64(encoded));
    }
    if (pixmap.isNull()) return false;
    image_label->setPixmap(pixmap);
    image_label->setFixedSize(pixmap.size());
    image_label->show();
    return true;
}

void PreviewPopup::clear_pending() {
    show_timer_.stop();
    pending_item_.reset();
    pending_pos_ = {};
    pending_prefer_side_ = "auto";
    pending_avoid_rect_.reset();
    current_item_id_.reset();
}

void PreviewPopup::hide_preview() {
    clear_pending();
    hide();
}

void PreviewPopup::force_cleanup() {
    clear_pending();
    if (isVisible()) hide();
}
}
