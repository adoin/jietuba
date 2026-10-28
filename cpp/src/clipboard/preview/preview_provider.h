#pragma once
#include "clipboard/core/models.h"
#include <QByteArray>
#include <QString>

namespace jietuba::clipboard {
class ClipboardManager;

// Presentation data, not a widget: the host owns rendering and resource policy.
struct PreviewContent {
    enum class Kind { Text, Html, Markdown, Image };
    Kind kind = Kind::Text;
    QString text;
    QByteArray image;
    QString provider_id;
};

// External preview plugins under plugin_root. None means the host should use its
// existing preview. An empty root uses the user's plugin directory.
std::optional<PreviewContent> resolve_preview(const ClipboardItem &item, ClipboardManager &manager, const QString &plugin_root = {}, const QString &mode = "preview",
                                              const QString &selected_format = {});
}
