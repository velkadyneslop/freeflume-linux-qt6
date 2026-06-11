// FreeFlume — runtime dependency check for the portable binary.
//
// Qt6 and libmpv are link-time dependencies: if they're missing the dynamic
// linker fails and we never reach main(), so by the time the app is running
// they're necessarily present. yt-dlp, on the other hand, is invoked as a
// subprocess — so it's the one dependency we can detect (and help with) at
// runtime. This shows a distro-aware popup if it isn't on PATH.
#pragma once

class QWidget;

namespace depcheck {
void warnIfMissing(QWidget* parent);
}
