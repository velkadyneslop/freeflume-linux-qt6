// FreeFlume — reveals per-row child widgets only while their row is hovered.
#pragma once

#include <QCursor>
#include <QEvent>
#include <QObject>
#include <QVector>
#include <QWidget>

// Coordinates "reveal on hover" buttons across the rows of a list. Entering any
// row hides every other row's target, so a button can't linger on a row that's
// no longer hovered even if its Leave event was dropped (e.g. fast movement
// between rows). Parent it to the page/list that owns the rows.
class HoverRevealGroup : public QObject {
public:
    using QObject::QObject;

    // Reveal `target` while `row` (or `target` itself) is hovered.
    void add(QWidget* row, QWidget* target) {
        target->hide();
        row->installEventFilter(this);
        target->installEventFilter(this);
        entries_.push_back({row, target});
    }

    // Forget all rows — call right before the list rebuilds its row widgets.
    void clear() { entries_.clear(); }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() != QEvent::Enter && event->type() != QEvent::Leave) {
            return false;
        }
        QWidget* row = nullptr;
        QWidget* target = nullptr;
        for (const Entry& e : entries_) {
            if (e.row == watched || e.target == watched) {
                row = e.row;
                target = e.target;
                break;
            }
        }
        if (!row) {
            return false;
        }
        if (event->type() == QEvent::Enter) {
            for (const Entry& e : entries_) {
                if (e.target != target) {
                    e.target->hide();  // clear any stale button on other rows
                }
            }
            target->show();
        } else {  // Leave: moving onto a child fires Leave on the row, so keep
                  // the button shown while the cursor is still within the row.
            if (!row->rect().contains(row->mapFromGlobal(QCursor::pos()))) {
                target->hide();
            }
        }
        return false;
    }

private:
    struct Entry {
        QWidget* row;
        QWidget* target;
    };
    QVector<Entry> entries_;
};
