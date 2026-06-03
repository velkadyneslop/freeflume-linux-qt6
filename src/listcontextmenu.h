// FreeFlume — reliable right-click menus for QListWidgets that use item widgets.
//
// Qt's per-widget CustomContextMenu policy (and the synthesized ContextMenu
// event) is flaky over item widgets: whether it fires depends on which row is
// current/focused, so the first right-click on an unselected row often does
// nothing. Mouse-press events, on the other hand, propagate 100% reliably from
// the row widgets up to the list's viewport (that's why left-click selection
// always works). So we drive the menu off the right-button press: resolve the
// row by cursor position (selection-independent), select it, and show the menu.
//
// The menu is shown on the next event-loop turn so that any already-open menu
// has fully closed and released its grab first — otherwise a second right-click
// in a row can silently fail.
//
// Usage: ListContextMenu::install(list, [](QListWidgetItem* it, QPoint g){ ... });
// The filter is parented to the list, so it lives and dies with it. Row widgets
// must keep the default context-menu policy (don't set CustomContextMenu).
#pragma once

#include <functional>
#include <utility>

#include <QContextMenuEvent>
#include <QEvent>
#include <QListWidget>
#include <QMouseEvent>
#include <QObject>
#include <QPoint>
#include <QSignalBlocker>

class ListContextMenu : public QObject {
public:
    using Handler = std::function<void(QListWidgetItem*, const QPoint& globalPos)>;

    static void install(QListWidget* list, Handler handler) {
        new ListContextMenu(list, std::move(handler));
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::RightButton) {
                requestMenu(me->globalPosition().toPoint());
                return true;  // we handle the menu; don't let the view also react
            }
            break;
        }
        case QEvent::ContextMenu: {
            // Mouse-reason events are handled via the press above; consume them
            // so we never get a duplicate menu. Honour the keyboard menu key.
            auto* e = static_cast<QContextMenuEvent*>(event);
            if (e->reason() == QContextMenuEvent::Keyboard) {
                requestMenu(e->globalPos());
            }
            return true;
        }
        default:
            break;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    ListContextMenu(QListWidget* list, Handler handler)
        : QObject(list), list_(list), handler_(std::move(handler)) {
        list_->viewport()->installEventFilter(this);
    }

    void requestMenu(const QPoint& globalPos) {
        const QPoint vp = list_->viewport()->mapFromGlobal(globalPos);
        QListWidgetItem* item = list_->itemAt(vp);
        if (!item) {
            return;
        }
        // Highlight the row the menu acts on, but block the selection signal so
        // it doesn't trigger navigation (e.g. loading a channel's feed).
        {
            const QSignalBlocker block(list_);
            list_->setCurrentItem(item);
        }
        Handler h = handler_;
        QMetaObject::invokeMethod(
            this, [h, item, globalPos] { h(item, globalPos); }, Qt::QueuedConnection);
    }

    QListWidget* list_;
    Handler handler_;
};
