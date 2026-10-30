#pragma once
#include <QColor>
#include <QFrame>

namespace jietuba::clipboard {
// Shared material for the panel and its recessed content wells.
class SurfaceFrame : public QFrame {
public:
    explicit SurfaceFrame(bool recessed, QWidget *parent = nullptr);
    void set_surface(const QColor &color, int radius);
protected:
    void paintEvent(QPaintEvent *event) override;
private:
    QColor color_{Qt::white};
    int radius_ = 12;
    bool recessed_;
};
}
