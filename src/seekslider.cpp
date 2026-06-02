// FreeFlume — seek slider implementation.
#include "seekslider.h"

#include <QMouseEvent>
#include <QPainter>
#include <QProxyStyle>
#include <QStyle>
#include <QStyleOptionSlider>

namespace {
// Makes a left-click on the groove jump straight to that position.
class AbsoluteClickStyle : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;
    int styleHint(StyleHint hint, const QStyleOption* option = nullptr,
                  const QWidget* widget = nullptr,
                  QStyleHintReturn* returnData = nullptr) const override {
        if (hint == SH_Slider_AbsoluteSetButtons) {
            return Qt::LeftButton | Qt::MiddleButton;
        }
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
};
}  // namespace

SeekSlider::SeekSlider(Qt::Orientation orientation, QWidget* parent)
    : QSlider(orientation, parent) {
    setMouseTracking(true);
    auto* style = new AbsoluteClickStyle;
    style->setParent(this);
    setStyle(style);
}

void SeekSlider::mouseMoveEvent(QMouseEvent* event) {
    if (orientation() == Qt::Horizontal && width() > 0) {
        const int x = event->position().toPoint().x();
        const int value = QStyle::sliderValueFromPosition(minimum(), maximum(), x, width());
        const double span = static_cast<double>(maximum() - minimum());
        const double fraction = span > 0 ? (value - minimum()) / span : 0.0;
        emit hovered(fraction, x);
    }
    QSlider::mouseMoveEvent(event);
}

void SeekSlider::leaveEvent(QEvent* event) {
    emit hoverLeft();
    QSlider::leaveEvent(event);
}

void SeekSlider::setChapterMarks(const QList<double>& fractions) {
    chapterMarks_ = fractions;
    update();
}

void SeekSlider::setSegments(const QList<SeekSegment>& segments) {
    segments_ = segments;
    update();
}

void SeekSlider::paintEvent(QPaintEvent* event) {
    QSlider::paintEvent(event);
    if ((chapterMarks_.isEmpty() && segments_.isEmpty()) || orientation() != Qt::Horizontal) {
        return;
    }
    QStyleOptionSlider opt;
    initStyleOption(&opt);
    const QRect groove = style()->subControlRect(QStyle::CC_Slider, &opt,
                                                 QStyle::SC_SliderGroove, this);
    const int handle = style()
                           ->subControlRect(QStyle::CC_Slider, &opt,
                                            QStyle::SC_SliderHandle, this)
                           .width();
    const int avail = groove.width() - handle;
    if (avail <= 0) {
        return;
    }
    const int x0 = groove.x() + handle / 2;
    QPainter p(this);
    p.setPen(Qt::NoPen);

    // Coloured segment bands (drawn first, under the chapter ticks).
    for (const SeekSegment& s : segments_) {
        const double a = qBound(0.0, s.start, 1.0);
        const double b = qBound(0.0, s.end, 1.0);
        if (b <= a) {
            continue;
        }
        const int xa = x0 + static_cast<int>(a * avail);
        const int xb = x0 + static_cast<int>(b * avail);
        QColor c = s.color;
        c.setAlpha(200);
        p.setBrush(c);
        p.drawRect(xa, groove.y(), qMax(2, xb - xa), groove.height());
    }

    // Chapter ticks.
    QColor c = palette().color(QPalette::WindowText);
    c.setAlpha(150);
    p.setBrush(c);
    for (double f : chapterMarks_) {
        if (f <= 0.0 || f >= 1.0) {
            continue;
        }
        const int x = x0 + static_cast<int>(f * avail);
        p.drawRect(x - 1, groove.y(), 2, groove.height());
    }
}
