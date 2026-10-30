#include "core/material_style.h"
#include "core/ui_theme.h"
#include "core/ui_scale.h"
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QEvent>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QLineEdit>
#include <QLabel>
#include <QLinearGradient>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QRegularExpression>
#include <QTextEdit>
#include <QWidget>
#include <QPointer>
#include <QVariantAnimation>
#include <cmath>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <algorithm>

namespace jietuba {
MaterialPalette material_palette(const QColor &accent, bool dark) {
    const float hue=std::max(0.0f,accent.hslHueF());
    const float saturation=accent.hslSaturationF();
    // Preserve the chosen hue; material depth comes from S/L, not black overlays.
    return dark ? MaterialPalette{QColor::fromHslF(hue,saturation*0.18f,0.16f),
                                  QColor::fromHslF(hue,saturation*0.24f,0.095f),
                                  QColor::fromHslF(hue,saturation*0.12f,0.245f)}
                : MaterialPalette{QColor::fromHslF(hue,saturation*0.12f,0.967f),
                                  QColor::fromHslF(hue,saturation*0.20f,0.84f),
                                  QColor::fromHslF(hue,saturation*0.08f,0.995f)};
}
const UIThemeTokens &material_tokens(const QWidget *widget) {
    for (auto *parent = widget; parent; parent = parent->parentWidget()) {
        const auto override = parent->property("_materialDark");
        if (override.isValid()) return override.toBool() ? DARK_TOKENS() : LIGHT_TOKENS();
    }
    return get_ui_theme()->tokens();
}

namespace {
int shadow_animation_duration() {
    if(qApp && qApp->property("_materialReducedMotion").toBool()) return 0;
#ifdef Q_OS_WIN
    BOOL enabled=TRUE;
    if(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&enabled,0) && !enabled) return 0;
#endif
    return 300;
}

void paint_outer_shadows(QPainter &p, const QRectF &body, qreal radius, const MaterialPalette &colors, int spread, qreal reverse=0) {
    p.save();p.setRenderHint(QPainter::Antialiasing);p.setPen(Qt::NoPen);
    QPainterPath outside;outside.addRect(body.adjusted(-spread*3,-spread*3,spread*3,spread*3));outside.addRoundedRect(body,radius,radius);
    p.setClipPath(outside,Qt::IntersectClip);
    const qreal offset=std::max(1.0,spread*0.45);
    for(int layer=spread;layer>=0;--layer) {
        const qreal weight=std::exp(-2.5*layer*layer/std::max(1.0,qreal(spread*spread)))/std::max(1,spread);
        for(int side : {-1,1}) {
            const QColor normal=side<0?colors.highlight:colors.shadow;
            const QColor inverted=side<0?colors.shadow:colors.highlight;
            QColor color=QColor::fromRgbF(normal.redF()*(1-reverse)+inverted.redF()*reverse,
                                         normal.greenF()*(1-reverse)+inverted.greenF()*reverse,
                                         normal.blueF()*(1-reverse)+inverted.blueF()*reverse);
            color.setAlphaF(std::min(1.0,weight*0.9));p.setBrush(color);
            p.drawRoundedRect(body.adjusted(-layer,-layer,layer,layer).translated(side*offset,side*offset),radius+layer,radius+layer);
        }
    }
    p.restore();
}
QColor parse_color(const QString &text) {
    const auto value = text.trimmed();
    if (!value.startsWith("rgb")) return QColor(value);
    const auto channels = value.mid(value.indexOf('(') + 1).chopped(1).split(',');
    if (channels.size() < 3) return {};
    QColor color(channels[0].trimmed().toInt(), channels[1].trimmed().toInt(), channels[2].trimmed().toInt());
    if (channels.size() == 4) color.setAlphaF(std::clamp(channels[3].trimmed().toDouble(), 0.0, 1.0));
    return color;
}

class MaterialDecoration : public QWidget {
public:
    explicit MaterialDecoration(QWidget *parent) : QWidget(parent) {
        setObjectName("_materialEdge");
        setProperty("_materialDecoration", true);
        setStyleSheet("QWidget { background: transparent; border: none; }");
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
        animation=new QVariantAnimation(this);
        animation->setEasingCurve(QEasingCurve::InOutCubic);
        connect(animation,&QVariantAnimation::valueChanged,this,[this](const QVariant &value) {
            progress=value.toReal();parentWidget()->setProperty("_materialHover",progress);update();if(ambient) ambient->update();
        });
        parent->installEventFilter(this);
        setGeometry(parent->rect());
        show();
    }
    MaterialDepth depth = MaterialDepth::Raised;
    int radius = 10;
    bool idle_flat = false;
    qreal progress=0;
    QVariantAnimation *animation=nullptr;
    QPointer<QWidget> ambient;
    void animate_to(qreal target) {
        animation->stop();animation->setDuration(shadow_animation_duration());
        animation->setStartValue(progress);animation->setEndValue(target);animation->start();
    }
protected:
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::Resize) setGeometry(parentWidget()->rect());
        switch (event->type()) {
        case QEvent::Enter: case QEvent::FocusIn: case QEvent::MouseButtonPress:
            if(qobject_cast<QAbstractButton *>(parentWidget())) animate_to(1);update();break;
        case QEvent::Leave: case QEvent::FocusOut: case QEvent::MouseButtonRelease:
            if(qobject_cast<QAbstractButton *>(parentWidget())) animate_to(event->type()==QEvent::Leave?0:(parentWidget()->underMouse()?1:0));update();break;
        case QEvent::Resize: case QEvent::Show: case QEvent::StyleChange:
            raise(); [[fallthrough]];
        case QEvent::EnabledChange:
            update(); break;
        default: break;
        }
        return false;
    }
    void paintEvent(QPaintEvent *) override {
        auto *owner = parentWidget();
        if (owner->property("_materialNative").toBool()) return;
        const auto *button = qobject_cast<QAbstractButton *>(owner);
        const bool pressed = button && (button->isDown() || button->isChecked() || owner->property("_materialSelected").toBool());
        if (idle_flat && !owner->underMouse() && !owner->hasFocus() && !pressed && progress<0.001) return;
        const auto &tokens = material_tokens(owner);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        if(owner->property("_materialCompact").toBool()) {
            for(auto *ancestor=owner->parentWidget();ancestor;ancestor=ancestor->parentWidget()) {
                if(!ancestor->property("_materialNative").toBool()) continue;
                const int inset=ancestor->property("_materialSurfaceInset").toInt();
                const int corner=ancestor->property("_materialCornerRadius").toInt();
                const QPoint origin=mapFrom(ancestor,QPoint());
                QPainterPath clip;clip.addRoundedRect(QRectF(origin,ancestor->size()).adjusted(inset,inset,-inset,-inset),corner,corner);
                painter.setClipPath(clip,Qt::IntersectClip);break;
            }
        }
        painter.setOpacity(owner->isEnabled() ? 1.0 : 0.4);
        const QRectF bounds = QRectF(rect()).adjusted(1, 1, -1, -1);
        if(button && !pressed) {
            painter.save();painter.setOpacity((owner->isEnabled()?1.0:0.4)*(1-progress));
            paint_material_edges(painter,bounds,radius,QColor(tokens.window),MaterialDepth::Raised,widget_scaled(owner,5));painter.restore();
            painter.save();painter.setOpacity((owner->isEnabled()?1.0:0.4)*progress);
            paint_material_edges(painter,bounds,radius,QColor(tokens.window),MaterialDepth::Recessed,widget_scaled(owner,5));painter.restore();
        } else paint_material_edges(painter,bounds,radius,QColor(tokens.window),pressed?MaterialDepth::Recessed:depth,widget_scaled(owner,5));
        if (owner->hasFocus() && depth == MaterialDepth::Recessed) {
            QColor glow(tokens.accent);
            glow.setAlpha(18);
            painter.setPen(QPen(glow, widget_scaled(owner, 5)));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(bounds.adjusted(2, 2, -2, -2), radius, radius);
        }
    }
};

class MaterialOuterShadow : public QWidget {
    QPointer<QWidget> owner;
    QPointer<MaterialDecoration> edge;
    void sync() {
        if(!owner || !owner->parentWidget() || owner->isWindow()) {hide();return;}
        if(parentWidget()!=owner->parentWidget()) setParent(owner->parentWidget());
        const int gutter=widget_scaled(owner,12);
        setGeometry(owner->geometry().adjusted(-gutter,-gutter,gutter,gutter));
        setVisible(owner->isVisible());stackUnder(owner);update();
    }
public:
    MaterialOuterShadow(QWidget *widget,MaterialDecoration *decoration) : QWidget(widget->parentWidget()),owner(widget),edge(decoration) {
        setProperty("_materialDecoration",true);setObjectName("_materialShadow");
        setStyleSheet("QWidget { background: transparent; border: none; }");
        setAttribute(Qt::WA_TransparentForMouseEvents);setFocusPolicy(Qt::NoFocus);
        widget->installEventFilter(this);connect(widget,&QObject::destroyed,this,&QObject::deleteLater);sync();
    }
protected:
    bool eventFilter(QObject *,QEvent *event) override {
        switch(event->type()) {
        case QEvent::Move:case QEvent::Resize:case QEvent::Show:case QEvent::Hide:case QEvent::ParentChange:case QEvent::ZOrderChange:case QEvent::StyleChange:sync();break;
        default:break;
        }
        return false;
    }
    void paintEvent(QPaintEvent *) override {
        if(!owner || !edge || owner->property("_materialNative").toBool()) return;
        if(edge->idle_flat && edge->progress<0.001 && !owner->underMouse() && !owner->hasFocus()) return;
        const auto &t=material_tokens(owner);
        const int gutter=widget_scaled(owner,12);
        const QRectF body=QRectF(rect()).adjusted(gutter,gutter,-gutter,-gutter);
        QPainter p(this);p.setOpacity(owner->isEnabled()?1.0:0.4);
        for(auto *ancestor=parentWidget();ancestor;ancestor=ancestor->parentWidget()) {
            if(!ancestor->property("_materialNative").toBool()) continue;
            const auto stored_inset=ancestor->property("_materialSurfaceInset");
            const int inset=stored_inset.isValid()?stored_inset.toInt():widget_scaled(ancestor,5);
            const auto stored_radius=ancestor->property("_materialCornerRadius");
            const int radius=stored_radius.isValid()?stored_radius.toInt():widget_scaled(ancestor,8);
            const QPoint origin=mapFrom(ancestor,QPoint());
            QPainterPath clip;clip.addRoundedRect(QRectF(origin,ancestor->size()).adjusted(inset,inset,-inset,-inset),radius,radius);
            p.setClipPath(clip,Qt::IntersectClip);break;
        }
        paint_outer_shadows(p,body,edge->radius,material_palette(QColor(t.accent),t.is_dark()),widget_scaled(owner,7),edge->progress);
    }
};
}

QString material_stylesheet(const QWidget *widget, const QString &sheet) {
    if (widget && widget->property("_materialDecoration").toBool()) return sheet;
    QString result = sheet;
    static const QRegularExpression stroke(R"((\bborder(?:-(?:top|bottom|left|right))?\s*:\s*[\d.]+px\s+)(?:solid|dashed|dotted)\s+[^;}\n]+)");
    result.replace(stroke, "\\1solid transparent");
    static const QRegularExpression stroke_color(R"(\bborder-(?:color|top-color|bottom-color|left-color|right-color)\s*:\s*[^;}\n]+)");
    result.replace(stroke_color, "border-color: transparent");
    // Theme-neutral surfaces share one opaque material; semantic accent fills keep their colour.
    static const QRegularExpression background(R"((\bbackground(?:-color)?\s*:\s*)(#[0-9a-fA-F]{3,8}\b|rgba?\([^)]*\)|white\b))");
    auto matches = background.globalMatch(result);
    QList<QPair<qsizetype, QPair<qsizetype, QString>>> replacements;
    const auto &tokens = material_tokens(widget);
    while (matches.hasNext() && !(widget && widget->property("_materialPreserveColor").toBool())) {
        const auto match = matches.next();
        const QColor color = parse_color(match.captured(2));
        if (!color.isValid() || color.alphaF() < 0.7) continue;
        const int low = std::min({color.red(), color.green(), color.blue()});
        const int high = std::max({color.red(), color.green(), color.blue()});
        if (match.captured(2).compare(tokens.accent_soft,Qt::CaseInsensitive)==0 || match.captured(2).compare(tokens.danger_soft,Qt::CaseInsensitive)==0) continue;
        if ((low >= 225 && high-low <= 8) || (tokens.is_dark() && high <= 90 && high-low <= 18))
            replacements.append({match.capturedStart(2), {match.capturedLength(2), tokens.window}});
    }
    for (auto i = replacements.crbegin(); i != replacements.crend(); ++i) result.replace(i->first, i->second.first, i->second.second);
    static const QRegularExpression separator(R"((QMenu::separator\s*\{[^}]*?background(?:-color)?\s*:)\s*[^;}]+)");
    result.replace(separator, "\\1 transparent");
    if (tokens.is_dark()) {
        static const QRegularExpression ink(R"((?<![-\w])color\s*:\s*(#[0-9a-fA-F]{3,6})\b)");
        auto colors=ink.globalMatch(result);
        QList<QPair<qsizetype,QPair<qsizetype,QString>>> inks;
        while(colors.hasNext()) {
            const auto match=colors.next(); const QColor color(match.captured(1));
            if (!color.isValid()) continue;
            const int low=std::min({color.red(),color.green(),color.blue()}), high=std::max({color.red(),color.green(),color.blue()});
            if(high-low<30 && high<150) inks.append({match.capturedStart(1),{match.capturedLength(1),high<80?tokens.text:tokens.text_muted}});
        }
        for(auto i=inks.crbegin();i!=inks.crend();++i) result.replace(i->first,i->second.first,i->second.second);
    }
    return result;
}

void update_material_decoration(QWidget *widget, const QString &sheet) {
    if (!widget || widget->property("_materialDecoration").toBool() || widget->property("_materialNative").toBool()) return;
    if (qobject_cast<QCheckBox *>(widget) || qobject_cast<QRadioButton *>(widget)) return;
    if (widget->isWindow() && !qobject_cast<QMenu *>(widget)) return;
    const bool input = qobject_cast<QLineEdit *>(widget) || qobject_cast<QAbstractSpinBox *>(widget) || qobject_cast<QComboBox *>(widget) ||
                       qobject_cast<QTextEdit *>(widget) || qobject_cast<QPlainTextEdit *>(widget) || qobject_cast<QAbstractItemView *>(widget);
    static const QRegularExpression radius_pattern(R"(border-radius\s*:\s*([\d.]+)px)");
    QString surface_sheet = sheet;
    bool targeted = false;
    if (!widget->objectName().isEmpty()) {
        const auto rule = QRegularExpression("#" + QRegularExpression::escape(widget->objectName()) + R"(\b[^{}:\[]*\{([^}]+)\})").match(sheet);
        if (rule.hasMatch()) { surface_sheet = rule.captured(1); targeted = true; }
    }
    const auto radius = radius_pattern.match(surface_sheet);
    const bool button = qobject_cast<QAbstractButton *>(widget);
    if(button && !widget->property("_materialPrimary").toBool()) {
        auto *edge=static_cast<MaterialDecoration *>(widget->findChild<QWidget *>("_materialEdge",Qt::FindDirectChildrenOnly));
        if(edge) {if(edge->ambient) delete edge->ambient;delete edge;}
        return;
    }
    const bool menu = qobject_cast<QMenu *>(widget);
    if (menu) widget->setAttribute(Qt::WA_TranslucentBackground);
    if (!input && !button && !menu) {
        const bool surface = qobject_cast<QFrame *>(widget) || widget->objectName().contains("Surface") || widget->objectName().contains("Card") || widget->objectName().startsWith("Wizard");
        if (!surface || (widget->styleSheet().isEmpty() && !targeted) || !radius.hasMatch() || qobject_cast<QLabel *>(widget)) return;
        if (surface_sheet.contains(QRegularExpression(R"(background(?:-color)?\s*:\s*transparent)"))) return;
    }
    if (input) {
        const QString type = qobject_cast<QLineEdit *>(widget) ? "QLineEdit" : qobject_cast<QComboBox *>(widget) ? "QComboBox" :
                             qobject_cast<QAbstractSpinBox *>(widget) ? "QSpinBox" : qobject_cast<QTextEdit *>(widget) ? "QTextEdit" : "QAbstractItemView";
        const auto match = QRegularExpression("\\b" + type + R"([^{}:]*\{([^}]+)\})").match(sheet);
        const QString own = match.hasMatch() ? match.captured(1) : sheet;
        if (own.contains(QRegularExpression(R"(background(?:-color)?\s*:\s*transparent)"))) return;
    }
    if (qobject_cast<QLineEdit *>(widget) && (qobject_cast<QAbstractSpinBox *>(widget->parentWidget()) || qobject_cast<QComboBox *>(widget->parentWidget()))) return;
    auto *edge = static_cast<MaterialDecoration *>(widget->findChild<QWidget *>("_materialEdge", Qt::FindDirectChildrenOnly));
    if (!edge) edge = new MaterialDecoration(widget);
    edge->depth = input || widget->property("_materialDepth").toString() == "recessed" ? MaterialDepth::Recessed : MaterialDepth::Raised;
    edge->radius = radius.hasMatch() ? qRound(radius.captured(1).toDouble()) : widget_scaled(widget, 8);
    if(widget->property("_materialRadius").isValid()) edge->radius=widget->property("_materialRadius").toInt();
    edge->idle_flat = !input && !menu && (surface_sheet.contains("background: transparent") || surface_sheet.contains("background-color: transparent"));
    edge->update();
    // Sibling shadows do not clip at the control edge or nest graphics effects.
    if(widget->property("_materialCompact").toBool() && edge->ambient) {delete edge->ambient;edge->ambient=nullptr;}
    if(!input && !menu && !widget->property("_materialCompact").toBool() && widget->parentWidget() && !edge->ambient) edge->ambient=new MaterialOuterShadow(widget,edge);
    if(edge->ambient) edge->ambient->update();
}

namespace {
class MaterialPolishFilter : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() != QEvent::Polish) return false;
        auto *widget = qobject_cast<QWidget *>(object);
        if (!widget || widget->property("_materialDecoration").toBool()) return false;
        QString sheet;
        for (auto *parent = widget; parent && sheet.isEmpty(); parent = parent->parentWidget()) sheet = parent->styleSheet();
        if (sheet.isEmpty() && qApp) sheet = qApp->styleSheet();
        if (!sheet.isEmpty()) update_material_decoration(widget, sheet);
        return false;
    }
};
}

void install_material_polish_filter(QObject *application) {
    if (!application || application->property("_materialPolishInstalled").toBool()) return;
    application->setProperty("_materialPolishInstalled",true);
    application->installEventFilter(new MaterialPolishFilter(application));
}

void paint_material_edges(QPainter &painter, const QRectF &bounds, qreal radius, const QColor &base, MaterialDepth depth, int spread) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip; clip.addRoundedRect(bounds, radius, radius);
    painter.setClipPath(clip, Qt::IntersectClip);
    painter.setBrush(Qt::NoBrush);
    const bool dark = base.lightnessF() < 0.5;
    const bool recessed = depth == MaterialDepth::Recessed;
    const QColor accent(get_ui_theme()->tokens().accent);
    const auto colors=material_palette(accent,dark);
    painter.setPen(Qt::NoPen);
    const int layers=std::max(1,spread);
    const qreal offset=layers*0.55;
    for(int i=layers;i>=0;--i) {
        const qreal weight=std::exp(-2.5*i*i/std::max(1.0,qreal(layers*layers)))/layers;
        for(int side : {-1,1}) {
            // A shifted, blurred hole gives a true inset shadow on each edge.
            QColor color=(side<0)==recessed?colors.shadow:colors.highlight;
            color.setAlphaF(std::min(1.0,weight*(recessed?0.95:0.6)));
            QPainterPath mask;mask.addRect(bounds.adjusted(-layers*3,-layers*3,layers*3,layers*3));
            mask.addRoundedRect(bounds.adjusted(-i,-i,i,i).translated(-side*offset,-side*offset),radius+i,radius+i);
            painter.fillPath(mask,color);
        }
    }
    painter.restore();
}

void paint_material_surface(QPainter &painter, const QRectF &bounds, qreal radius, const QColor &base, MaterialDepth depth) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen); painter.setBrush(base);
    painter.drawRoundedRect(bounds,radius,radius);
    paint_material_edges(painter,bounds,radius,base,depth);
    painter.restore();
}

void paint_material_window(QWidget *widget, int radius, const QColor &base) {
    if (!widget->property("_materialNative").toBool()) widget->setProperty("_materialNative", true);
    QPainter painter(widget);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto &tokens = material_tokens(widget);
    const QColor material = base.isValid() ? base : QColor(tokens.window);
    if (widget->isWindow()) {
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(widget->rect(), Qt::transparent);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    }
    const int inset = widget_scaled(widget, 5);
    if(widget->property("_materialSurfaceInset").toInt()!=inset) widget->setProperty("_materialSurfaceInset",inset);
    if(widget->property("_materialCornerRadius").toInt()!=radius) widget->setProperty("_materialCornerRadius",radius);
    const QRectF body = QRectF(widget->rect()).adjusted(inset,inset,-inset,-inset);
    painter.setPen(Qt::NoPen);
    const auto colors=material_palette(QColor(tokens.accent),tokens.is_dark());
    paint_outer_shadows(painter,body,radius,colors,std::max(1,inset-2));
    paint_material_surface(painter,body,radius,material,MaterialDepth::Raised);
}
}
