// FreeFlume — seek slider with click-to-seek and hover reporting.
#pragma once

#include <QColor>
#include <QList>
#include <QSlider>

// A coloured region on the seek groove (e.g. a SponsorBlock segment), with
// start/end as fractions in [0,1].
struct SeekSegment {
    double start = 0.0;
    double end = 0.0;
    QColor color;
};

// A horizontal slider where a left-click jumps to the clicked position (rather
// than stepping), reports the hovered position for a preview, and draws chapter
// markers on the groove.
class SeekSlider : public QSlider {
    Q_OBJECT

public:
    explicit SeekSlider(Qt::Orientation orientation, QWidget* parent = nullptr);

    // Chapter boundary positions as fractions in [0,1] (drawn as tick marks).
    void setChapterMarks(const QList<double>& fractions);

    // Coloured regions on the groove (e.g. SponsorBlock segments).
    void setSegments(const QList<SeekSegment>& segments);

signals:
    // fraction in [0,1] of the hovered point; xInSlider is its x in slider coords.
    void hovered(double fraction, int xInSlider);
    void hoverLeft();

protected:
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QList<double> chapterMarks_;
    QList<SeekSegment> segments_;
};
