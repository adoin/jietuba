#include "core/ui_theme.h"
#include "core/material_style.h"
#include "core/theme.h"
#include "settings/tool_settings.h"
#include <QApplication>
#include <QGuiApplication>
#include <QStyleHints>
#include <QWidget>
#include <cmath>

namespace jietuba {
QString ui_theme_mode_value(UIThemeMode mode) {
    switch (mode) {
    case UIThemeMode::LIGHT: return "light";
    case UIThemeMode::DARK: return "dark";
    default: return "system";
    }
}

UIThemeMode coerce_ui_theme_mode(const QVariant &value) {
    const auto text = value.toString().toLower();
    if (text == "light") return UIThemeMode::LIGHT;
    if (text == "dark") return UIThemeMode::DARK;
    return UIThemeMode::SYSTEM;
}

namespace {
void apply_accent(UIThemeTokens &t) {
    const QColor accent = get_theme().theme_color();
    static QString light_accent, dark_accent;
    auto &cached=t.is_dark()?dark_accent:light_accent;
    if(cached==accent.name()) return;
    cached=accent.name();
    const auto material=material_palette(accent,t.is_dark());
    t.window=t.window_top=t.window_bottom=material.surface.name();
    t.surface=t.surface_strong=t.surface_subtle=t.input_background=t.popup_background=t.window;
    t.surface_hover=QColor::fromHslF(std::max(0.0f,accent.hslHueF()),accent.hslSaturationF()*0.14,t.is_dark()?0.19:0.95).name();
    t.accent = accent.name();
    t.accent_hover = accent.darker(108).name();
    t.accent_pressed = accent.darker(118).name();
    auto luminance=[](const QColor &color) {
        auto linear=[](double c){return c<=0.04045?c/12.92:std::pow((c+0.055)/1.055,2.4);};
        return 0.2126*linear(color.redF())+0.7152*linear(color.greenF())+0.0722*linear(color.blueF());
    };
    QColor strong=accent;
    while(luminance(strong)>0.18) strong=strong.darker(106);
    t.accent_strong=strong.name();
    t.accent_strong_hover=strong.darker(108).name();
    t.accent_strong_pressed=strong.darker(118).name();
    const QColor base(t.window);
    const double mix=t.is_dark()?0.22:0.14;
    t.accent_soft=QColor(qRound(base.red()*(1-mix)+accent.red()*mix),qRound(base.green()*(1-mix)+accent.green()*mix),qRound(base.blue()*(1-mix)+accent.blue()*mix)).name();
    QColor ink=accent;
    if(t.is_dark()) { while(luminance(ink)<0.48) { if(ink.value()<20) ink=QColor(80,80,80); else ink=ink.lighter(110); } }
    else { while(luminance(ink)>0.16) ink=ink.darker(108); }
    t.accent_text=ink.name();
    t.popup_hover=t.accent_soft;
}
UIThemeTokens common_tokens(UIThemeMode mode) {
    UIThemeTokens t;
    t.mode = mode;
    t.accent = "#6F8FAB";
    t.accent_hover = "#627F99";
    t.accent_pressed = "#526D85";
    t.accent_strong = "#58748D";
    t.accent_strong_hover = "#4F6A82";
    t.accent_strong_pressed = "#465E73";
    return t;
}
}

const UIThemeTokens &LIGHT_TOKENS() {
    static UIThemeTokens tokens = [] {
        auto t = common_tokens(UIThemeMode::LIGHT);
        t.accent_soft = "#DFE8EF";
        t.accent_text = "#465E73";
        t.window = t.window_top = t.window_bottom = "#F7F7F8";
        t.surface = t.surface_strong = t.surface_subtle = t.window;
        t.surface_hover = "#F0F1F3";
        t.text = "#17201B";
        t.text_muted = "#68736D";
        t.text_disabled = "#9AA39D";
        t.border = "rgba(112, 130, 119, 0.20)";
        t.border_hover = "rgba(76, 101, 86, 0.34)";
        t.window_border = "#D3D7DC";
        t.input_background = t.popup_background = t.window;
        t.popup_hover = "#EAF2FA";
        t.separator = "transparent";
        t.switch_off = "#C4CCC6";
        t.success = "#3E9B63";
        t.danger = "#CF4F4B";
        t.danger_soft = "#FBECEB";
        t.status_idle = "#BCC5BF";
        return t;
    }();
    apply_accent(tokens);
    return tokens;
}

const UIThemeTokens &DARK_TOKENS() {
    static UIThemeTokens tokens = [] {
        auto t = common_tokens(UIThemeMode::DARK);
        t.accent_soft = "#31404C";
        t.accent_text = "#B9CCDD";
        t.window = t.window_top = t.window_bottom = "#25292E";
        t.surface = t.surface_strong = t.surface_subtle = t.window;
        t.surface_hover = "#30363D";
        t.text = "#F2F4F6";
        t.text_muted = "#AEB5BD";
        t.text_disabled = "#737B84";
        t.border = "rgba(255, 255, 255, 0.12)";
        t.border_hover = "rgba(255, 255, 255, 0.28)";
        t.window_border = "#3B424A";
        t.input_background = t.popup_background = t.window;
        t.popup_hover = "#363B42";
        t.separator = "transparent";
        t.switch_off = "#626A73";
        t.success = "#4FAE74";
        t.danger = "#E0605B";
        t.danger_soft = "rgba(224, 96, 91, 0.14)";
        t.status_idle = "#5A6168";
        return t;
    }();
    apply_accent(tokens);
    return tokens;
}

UIThemeManager::UIThemeManager(QObject *parent) : QObject(parent) {}

UIThemeManager *UIThemeManager::init(ToolSettingsManager *config_manager, QApplication *application) {
    install_material_polish_filter(application ? application : qApp);
    config_ = config_manager;
    application_ = application ? application : qobject_cast<QApplication *>(QCoreApplication::instance());
    const auto saved = config_manager ? QVariant(config_manager->get_ui_theme_mode()) : QVariant("system");
    mode_ = coerce_ui_theme_mode(saved);
    connect_system_theme();
    apply(true);
    return this;
}

void UIThemeManager::set_mode(const QVariant &mode, bool persist) {
    const auto normalized = coerce_ui_theme_mode(mode);
    const bool changed = normalized != mode_;
    mode_ = normalized;
    if (persist && config_) config_->set_ui_theme_mode(ui_theme_mode_value(normalized));
    apply(changed);
    if (changed) emit mode_changed(ui_theme_mode_value(normalized));
}

void UIThemeManager::apply(bool force) {
    const auto effective = resolve_effective_mode();
    const bool changed = effective != effective_mode_;
    effective_mode_ = effective;
    if (!(force || changed)) return;
    if (application_) {
        application_->setProperty("uiTheme", ui_theme_mode_value(effective));
        application_->setPalette(build_palette(tokens()));
        application_->setStyleSheet(material_stylesheet(nullptr, build_global_stylesheet(tokens())));
    }
    emit theme_changed(tokens());
}

UIThemeMode UIThemeManager::resolve_effective_mode() const {
    if (mode_ != UIThemeMode::SYSTEM) return mode_;
    if (auto *hints = QGuiApplication::styleHints()) {
        const auto scheme = hints->colorScheme();
        if (scheme == Qt::ColorScheme::Dark) return UIThemeMode::DARK;
        if (scheme == Qt::ColorScheme::Light) return UIThemeMode::LIGHT;
    }
    // Some remote-desktop and older platform plugins report Unknown.
    auto *app = application_ ? application_ : qobject_cast<QApplication *>(QCoreApplication::instance());
    if (app) return app->palette().color(QPalette::Window).lightness() < 128 ? UIThemeMode::DARK : UIThemeMode::LIGHT;
    return UIThemeMode::LIGHT;
}

void UIThemeManager::connect_system_theme() {
    if (system_signal_connected_) return;
    if (auto *hints = QGuiApplication::styleHints()) {
        connect(hints, &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme) {
            if (mode_ == UIThemeMode::SYSTEM) apply();
        });
        system_signal_connected_ = true;
    }
}

QPalette UIThemeManager::build_palette(const UIThemeTokens &t) {
    QPalette palette;
    const std::pair<QPalette::ColorRole, QString> roles[] = {
        {QPalette::Window, t.window},
        {QPalette::WindowText, t.text},
        {QPalette::Base, t.input_background},
        {QPalette::AlternateBase, t.surface_subtle},
        {QPalette::ToolTipBase, t.popup_background},
        {QPalette::ToolTipText, t.text},
        {QPalette::Text, t.text},
        {QPalette::Button, t.surface_strong},
        {QPalette::ButtonText, t.text},
        {QPalette::BrightText, "#FFFFFF"},
        {QPalette::Link, t.accent},
        {QPalette::Highlight, t.accent},
        {QPalette::HighlightedText, t.selected_text},
        {QPalette::PlaceholderText, t.text_muted},
        {QPalette::Mid, t.border_hover},
        {QPalette::Dark, t.window_border},
    };
    for (const auto group : {QPalette::Normal, QPalette::Inactive, QPalette::Disabled})
        for (const auto &[role, value] : roles) palette.setColor(group, role, QColor::fromString(value));
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::PlaceholderText})
        palette.setColor(QPalette::Disabled, role, QColor::fromString(t.text_disabled));
    return palette;
}

QString UIThemeManager::build_global_stylesheet(const UIThemeTokens &t) {
    // Kept small; feature windows with their own theme override it locally.
    return QString(R"(
            QLineEdit, QComboBox, QAbstractSpinBox, QTextEdit, QPlainTextEdit, QAbstractItemView {
                background: %2;
                color: %1;
                border: 1px solid transparent;
                border-radius: 8px;
                outline: none;
            }
            QToolTip {
                color: %1;
                background-color: %2;
                border: 1px solid %3;
                padding: 4px 7px;
            }
            QMenu {
                color: %1;
                background-color: %2;
                border: 1px solid %4;
                border-radius: 6px;
                padding: 4px;
            }
            QMenu::item {
                color: %1;
                background-color: transparent;
                padding: 6px 28px 6px 26px;
                margin: 1px 0;
            }
            QMenu::item:selected {
                color: %1;
                background-color: %5;
            }
            QMenu::item:disabled {
                color: %6;
                background-color: transparent;
            }
            QMenu::separator {
                height: 1px;
                background: %7;
                margin: 4px 7px;
            }
        )").arg(t.text, t.popup_background, t.border_hover, t.border, t.popup_hover, t.text_disabled, t.separator);
}

void apply_style_sheet(QWidget *widget, const QString &sheet) {
    const auto material = material_stylesheet(widget, sheet);
    if (widget->styleSheet() != material) widget->setStyleSheet(material);
    update_material_decoration(widget, material);
}

void set_own_style(QWidget *widget, const QString &declarations) {
    // Qt style sheets spell a namespaced class "ns--Class".
    const auto name = QString::fromLatin1(widget->metaObject()->className()).replace("::", "--");
    apply_style_sheet(widget, QString(".%1 { %2 }").arg(name, declarations));
}

UIThemeManager *get_ui_theme() {
    static UIThemeManager *manager = new UIThemeManager;
    return manager;
}
}
