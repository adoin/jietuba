#include "clipboard/ui/widgets/surface_frame.h"
#include "core/ui_scale.h"
#include "core/material_style.h"
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

namespace jietuba::clipboard {
SurfaceFrame::SurfaceFrame(bool recessed, QWidget *parent) : QFrame(parent), recessed_(recessed) {
    setProperty("_materialNative", true);
    setAutoFillBackground(false);
}

void SurfaceFrame::set_surface(const QColor &color, int radius) {
    color_ = color;
    radius_ = radius;
    update();
}

void SurfaceFrame::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath shape;
    shape.addRoundedRect(bounds, radius_, radius_);
    painter.fillPath(shape, color_);
    if (!recessed_) return;
    painter.setClipPath(shape);
    painter.setBrush(Qt::NoBrush);
    paint_material_edges(painter,bounds,radius_,color_,MaterialDepth::Recessed,scaled(6));
}
}
