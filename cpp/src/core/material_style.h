#pragma once
#include <QColor>
#include <QRectF>
#include <QString>

class QPainter;
class QWidget;
class QObject;

namespace jietuba {
struct UIThemeTokens;
enum class MaterialDepth { Raised, Recessed };
struct MaterialPalette { QColor surface, shadow, highlight; };
MaterialPalette material_palette(const QColor &accent, bool dark);
const UIThemeTokens &material_tokens(const QWidget *widget);
QString material_stylesheet(const QWidget *widget, const QString &sheet);
void update_material_decoration(QWidget *widget, const QString &sheet);
void install_material_polish_filter(QObject *application);
void paint_material_edges(QPainter &painter, const QRectF &bounds, qreal radius, const QColor &base, MaterialDepth depth, int spread = 6);
void paint_material_surface(QPainter &painter, const QRectF &bounds, qreal radius, const QColor &base, MaterialDepth depth);
void paint_material_window(QWidget *widget, int radius, const QColor &base = {});
}
