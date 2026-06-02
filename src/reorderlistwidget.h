// FreeFlume — a QListWidget whose rows can be reordered by dragging.
#pragma once

#include <QAbstractItemView>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QListWidget>
#include <QMimeData>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>

// Rows are reordered by drag. Because the list uses item widgets (which Qt's
// built-in InternalMove would discard, along with our non-serializable item
// data), this does NOT mutate the model itself — it emits itemMoved(from,
// insertRow) so the owner can reorder its own data and rebuild the rows.
class ReorderableListWidget : public QListWidget {
    Q_OBJECT

public:
    using QListWidget::QListWidget;

signals:
    void itemMoved(int from, int insertRow);

protected:
    void startDrag(Qt::DropActions) override {
        const int row = currentRow();
        if (row < 0) {
            return;
        }
        dragRow_ = row;
        auto* mime = new QMimeData;  // content is irrelevant; we move by index
        mime->setData(QStringLiteral("application/x-freeflume-row"), QByteArray("1"));
        auto* drag = new QDrag(this);
        drag->setMimeData(mime);
        if (QWidget* w = itemWidget(item(row))) {
            drag->setPixmap(w->grab());
            drag->setHotSpot(QPoint(24, w->height() / 2));
        }
        drag->exec(Qt::MoveAction);  // result ignored; the drop handler reorders
        dragRow_ = -1;
        dropRow_ = -1;
        viewport()->update();
    }

    void dragEnterEvent(QDragEnterEvent* e) override {
        if (e->source() == this) {
            e->acceptProposedAction();
        } else {
            e->ignore();
        }
    }

    void dragMoveEvent(QDragMoveEvent* e) override {
        if (e->source() != this) {
            e->ignore();
            return;
        }
        const int row = rowAt(e->position().toPoint());
        if (row != dropRow_) {
            dropRow_ = row;
            viewport()->update();  // redraw the indicator line
        }
        e->acceptProposedAction();
    }

    void dragLeaveEvent(QDragLeaveEvent* e) override {
        dropRow_ = -1;
        viewport()->update();
        QListWidget::dragLeaveEvent(e);
    }

    void dropEvent(QDropEvent* e) override {
        const int insertRow = dropRow_;
        dropRow_ = -1;
        viewport()->update();
        if (e->source() != this || dragRow_ < 0 || insertRow < 0) {
            e->ignore();
            return;
        }
        emit itemMoved(dragRow_, insertRow);
        e->acceptProposedAction();
    }

    // Paint a horizontal insertion line at the current drop target.
    void paintEvent(QPaintEvent* e) override {
        QListWidget::paintEvent(e);
        if (dropRow_ < 0) {
            return;
        }
        int y = 0;
        if (dropRow_ < count()) {
            y = visualItemRect(item(dropRow_)).top();
        } else if (count() > 0) {
            y = visualItemRect(item(count() - 1)).bottom();
        }
        QPainter painter(viewport());
        QColor c = palette().color(QPalette::Highlight);
        painter.setPen(QPen(c, 2));
        painter.drawLine(3, y, viewport()->width() - 3, y);
    }

private:
    // The row a drop at `p` would insert before (count() = after the last item).
    // Uses item centres rather than indexAt(), so the gaps between rows don't
    // momentarily snap the indicator to the bottom of the list.
    int rowAt(const QPoint& p) const {
        const int n = count();
        for (int i = 0; i < n; ++i) {
            if (p.y() < visualItemRect(item(i)).center().y()) {
                return i;  // in the top half of row i → insert before it
            }
        }
        return n;  // below every item's centre → append
    }

    int dragRow_ = -1;
    int dropRow_ = -1;
};
