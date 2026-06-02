// FreeFlume — user-configurable player keyboard shortcuts.
#pragma once

#include <QList>
#include <QString>

namespace shortcuts {

// A bindable player action: a stable id, a human label, and a default key.
struct Action {
    QString id;
    QString label;
    int defaultKey;  // a Qt::Key value, optionally OR'd with Qt::KeyboardModifiers
};

// The full list of configurable actions, in display order.
const QList<Action>& actions();

// The key currently bound to an action, as a combined key code
// (QKeyCombination::toCombined()). 0 means "unbound". Stale/invalid stored values
// fall back to the action's default.
int keyFor(const QString& id);

}  // namespace shortcuts
