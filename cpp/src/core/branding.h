#pragma once
#include "core/brand_config.h"
#include "core/i18n.h"
#include <QString>

namespace jietuba {
inline QString brand_name() {
    const auto language = I18nManager::get_current_language();
    return language.startsWith("zh") ? QString::fromUtf8(JTM_DISPLAY_NAME_ZH) : QStringLiteral(JTM_DISPLAY_NAME);
}
inline QString brand_display(QString text) {
    text.replace("jietuba", QStringLiteral(JTM_DISPLAY_NAME), Qt::CaseInsensitive);
    text.replace(QString::fromUtf8("截图吧"), QString::fromUtf8(JTM_DISPLAY_NAME_ZH));
    text.replace(QString::fromUtf8("截圖吧"), QString::fromUtf8(JTM_DISPLAY_NAME_ZH));
    return text;
}
}
