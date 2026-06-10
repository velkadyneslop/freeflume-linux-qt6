// FreeFlume — player page implementation.
#include "apppaths.h"
#include "playerpage.h"

#include "sharemenu.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QHBoxLayout>
#include <QHash>
#include <QMenu>
#include <QIcon>
#include <QImage>
#include <QImageWriter>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QStandardPaths>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include "extractor.h"
#include "htmlutil.h"
#include "database.h"
#include "downloadmenu.h"
#include "mpvwidget.h"
#include "playlistmenu.h"
#include "seekslider.h"
#include "shortcuts.h"
#include "sponsorcategories.h"
#include "thumbnailloader.h"
#include "videolist.h"

namespace {
constexpr int kAutoHideMs = 3000;

QString languageName(const QString& code) {
    static const QHash<QString, QString> kNames = {
        {QStringLiteral("en"), QStringLiteral("English")},
        {QStringLiteral("es"), QStringLiteral("Spanish")},
        {QStringLiteral("fr"), QStringLiteral("French")},
        {QStringLiteral("de"), QStringLiteral("German")},
        {QStringLiteral("it"), QStringLiteral("Italian")},
        {QStringLiteral("pt"), QStringLiteral("Portuguese")},
        {QStringLiteral("ru"), QStringLiteral("Russian")},
        {QStringLiteral("ja"), QStringLiteral("Japanese")},
        {QStringLiteral("ko"), QStringLiteral("Korean")},
        {QStringLiteral("zh"), QStringLiteral("Chinese")},
        {QStringLiteral("ar"), QStringLiteral("Arabic")},
        {QStringLiteral("hi"), QStringLiteral("Hindi")},
    };
    return kNames.value(code, code.toUpper());
}

QToolButton* iconButton(const QString& iconName, const QString& tip) {
    auto* b = new QToolButton;
    b->setIcon(QIcon::fromTheme(iconName));
    b->setToolTip(tip);
    b->setAutoRaise(true);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}

// Quality options: label → yt-dlp format string (caps the max height).
struct Quality {
    const char* label;
    const char* format;
};
constexpr Quality kQualities[] = {
    {"Best", "bestvideo+bestaudio/best"},  // highest available incl. 4K/8K, 60fps
    {"Auto", "bestvideo*+bestaudio/best"},
    {"2160p", "bestvideo[height<=2160]+bestaudio/best[height<=2160]/best"},
    {"1440p", "bestvideo[height<=1440]+bestaudio/best[height<=1440]/best"},
    {"1080p", "bestvideo[height<=1080]+bestaudio/best[height<=1080]/best"},
    {"720p", "bestvideo[height<=720]+bestaudio/best[height<=720]/best"},
    {"480p", "bestvideo[height<=480]+bestaudio/best[height<=480]/best"},
    {"360p", "bestvideo[height<=360]+bestaudio/best[height<=360]/best"},
};
}  // namespace

PlayerPage::PlayerPage(QWidget* parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    // The player floats over the live page, so it must paint an opaque
    // background — otherwise the page behind shows through its chrome.
    setAutoFillBackground(true);
    extractor_ = new Extractor(this);
    sponsor_ = new SponsorBlock(this);
    connect(sponsor_, &SponsorBlock::segmentsReady, this,
            [this](const QString& videoId, const QList<SponsorSegment>& segments) {
                if (videoId != share::videoId(currentUrl_)) {
                    return;  // a later video already started
                }
                segments_ = segments;
                updateSponsorMarks();
            });

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);

    // ---- top bar: Back · title · Info ----
    topBar_ = new QWidget(this);
    topBar_->setMouseTracking(true);
    auto* topRow = new QHBoxLayout(topBar_);
    topRow->setContentsMargins(8, 6, 8, 6);
    topRow->setSpacing(8);

    hamburgerBtn_ = iconButton(QStringLiteral("application-menu"), tr("Show/hide sidebar"));
    hamburgerBtn_->setVisible(false);  // only in windowed full playback
    connect(hamburgerBtn_, &QToolButton::clicked, this, &PlayerPage::toggleSidebarRequested);

    auto* backBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("go-previous")),
                                    tr("&Back"), topBar_);
    backBtn->setToolTip(tr("Back to results (Esc)"));
    backBtn->setFocusPolicy(Qt::NoFocus);

    titleLabel_ = new QLabel(tr("Nothing playing"), topBar_);
    QFont tf = titleLabel_->font();
    tf.setBold(true);
    titleLabel_->setFont(tf);
    titleLabel_->setTextInteractionFlags(Qt::NoTextInteraction);

    // Channel name under the title — a clickable link to visit the channel.
    channelLabel_ = new QLabel(topBar_);
    channelLabel_->setTextFormat(Qt::RichText);
    channelLabel_->setOpenExternalLinks(false);
    connect(channelLabel_, &QLabel::linkActivated, this, [this](const QString&) {
        if (!channelUrl_.isEmpty()) {
            emit channelRequested(channelUrl_);
        }
    });

    auto* titleCol = new QVBoxLayout;
    titleCol->setContentsMargins(0, 0, 0, 0);
    titleCol->setSpacing(0);
    titleCol->addWidget(titleLabel_);
    titleCol->addWidget(channelLabel_);

    queueBtn_ = iconButton(QStringLiteral("media-playlist-normal"), tr("Up next (Q)"));
    queueBtn_->setCheckable(true);
    queueBtn_->setVisible(false);  // only shown when a queue is active

    infoBtn_ = iconButton(QStringLiteral("documentinfo"), tr("Show description (I)"));
    infoBtn_->setCheckable(true);

    topRow->addWidget(hamburgerBtn_);
    topRow->addWidget(backBtn);
    topRow->addLayout(titleCol, /*stretch=*/1);
    topRow->addWidget(queueBtn_);
    topRow->addWidget(infoBtn_);
    col->addWidget(topBar_);

    // ---- middle: video | (collapsible) info panel ----
    split_ = new QSplitter(Qt::Horizontal, this);
    auto* split = split_;
    video_ = new MpvWidget(split);
    video_->setMinimumSize(160, 90);  // small enough for the mini-player / PiP
    video_->setMouseTracking(true);
    video_->installEventFilter(this);  // forward keys + mouse moves
    video_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(video_, &QWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) { showVideoContextMenu(pos); });
    split->addWidget(video_);

    infoPanel_ = new QWidget(split);
    auto* infoCol = new QVBoxLayout(infoPanel_);
    infoCol->setContentsMargins(10, 10, 10, 10);
    infoText_ = new QTextBrowser(infoPanel_);
    infoText_->setOpenLinks(false);  // we route links ourselves (channel vs web)
    infoText_->setFrameShape(QFrame::NoFrame);
    connect(infoText_, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        if (url.scheme() == QLatin1String("freeflume")) {
            if (url.host() == QLatin1String("seek")) {
                bool ok = false;
                const double secs = url.path().mid(1).toDouble(&ok);  // path is "/<secs>"
                if (ok) {
                    video_->seekAbsolute(secs);
                }
            } else if (!channelUrl_.isEmpty()) {
                emit channelRequested(channelUrl_);
            }
        } else {
            QDesktopServices::openUrl(url);  // real web links open in the browser
        }
    });
    infoCol->addWidget(infoText_);
    split->addWidget(infoPanel_);

    // "Up Next" queue panel (hidden until a playlist/queue is playing).
    queuePanel_ = new QWidget(split);
    auto* queueCol = new QVBoxLayout(queuePanel_);
    queueCol->setContentsMargins(8, 10, 8, 8);
    queueCol->setSpacing(6);
    queueHeader_ = new QLabel(tr("Up Next"), queuePanel_);
    QFont qhf = queueHeader_->font();
    qhf.setBold(true);
    queueHeader_->setFont(qhf);
    queueHeader_->setWordWrap(true);
    autoplayToggle_ = new QCheckBox(tr("Autoplay next"), queuePanel_);
    autoplayToggle_->setChecked(
        QSettings(apppaths::configFile(), QSettings::IniFormat).value(QStringLiteral("playback/autoplayNext"), true).toBool());
    connect(autoplayToggle_, &QCheckBox::toggled, this, [](bool on) {
        QSettings(apppaths::configFile(), QSettings::IniFormat).setValue(QStringLiteral("playback/autoplayNext"), on);
    });
    queueThumbs_ = new ThumbnailLoader(this);
    queueList_ = new VideoList(queueThumbs_, queuePanel_);
    connect(queueList_, &VideoList::activated, this, [this](const SearchResult& r) {
        for (int i = 0; i < queue_.size(); ++i) {
            if (queue_[i].url == r.url) {
                playIndex(i);
                return;
            }
        }
    });
    queueCol->addWidget(queueHeader_);
    queueCol->addWidget(autoplayToggle_);
    queueCol->addWidget(queueList_, 1);
    split->addWidget(queuePanel_);

    split->setStretchFactor(0, 4);
    split->setStretchFactor(1, 2);
    split->setStretchFactor(2, 2);
    infoPanel_->setVisible(false);
    queuePanel_->setVisible(false);

    col->addWidget(split, /*stretch=*/1);

    // ---- transport bar ----
    transportBar_ = new QWidget(this);
    transportBar_->setMouseTracking(true);
    // Two rows: the seek bar (full width), then the controls below it.
    auto* transportCol = new QVBoxLayout(transportBar_);
    transportCol->setContentsMargins(10, 4, 10, 6);
    transportCol->setSpacing(2);
    auto* row = new QHBoxLayout;  // the controls row
    row->setSpacing(8);

    prevBtn_ = iconButton(QStringLiteral("media-skip-backward"), tr("Previous (P)"));
    playBtn_ = iconButton(QStringLiteral("media-playback-start"), tr("Play/Pause (Space)"));
    nextBtn_ = iconButton(QStringLiteral("media-skip-forward"), tr("Next (N)"));
    prevBtn_->setVisible(false);  // only shown when a queue is active
    nextBtn_->setVisible(false);
    muteBtn_ = iconButton(QStringLiteral("audio-volume-high"), tr("Mute (M)"));
    fsBtn_ = iconButton(QStringLiteral("view-fullscreen"), tr("Fullscreen (F)"));
    loopBtn_ = iconButton(QStringLiteral("media-playlist-repeat"), tr("Loop (R)"));
    loopBtn_->setCheckable(true);
    connect(loopBtn_, &QToolButton::toggled, this, [this](bool on) { video_->setLoop(on); });
    pipBtn_ = iconButton(QStringLiteral("window-new"), tr("Picture-in-Picture"));
    connect(pipBtn_, &QToolButton::clicked, this, &PlayerPage::togglePip);
    screenshotBtn_ = iconButton(QStringLiteral("camera-photo"), tr("Take a screenshot"));
    connect(screenshotBtn_, &QToolButton::clicked, this, &PlayerPage::takeScreenshot);

    // Captions: click "CC" to toggle; the dropdown picks a track or toggles
    // auto-translate (whose target language lives in Settings).
    ccBtn_ = new QToolButton(transportBar_);
    ccBtn_->setText(QStringLiteral("CC"));
    ccBtn_->setCheckable(true);
    ccBtn_->setAutoRaise(true);
    ccBtn_->setFocusPolicy(Qt::NoFocus);
    ccBtn_->setToolTip(tr("Subtitles / CC (C) — menu for tracks & translate"));
    ccBtn_->setPopupMode(QToolButton::MenuButtonPopup);
    QFont ccFont = ccBtn_->font();
    ccFont.setBold(true);
    ccBtn_->setFont(ccFont);
    auto* ccMenu = new QMenu(ccBtn_);
    ccBtn_->setMenu(ccMenu);
    connect(ccMenu, &QMenu::aboutToShow, this, [this, ccMenu] {
        ccMenu->clear();
        const QList<MpvTrack> tracks = active()->subtitleTracks();
        bool anyOn = false;
        for (const MpvTrack& t : tracks) {
            anyOn = anyOn || t.selected;
        }
        QAction* off = ccMenu->addAction(tr("Off"));
        off->setCheckable(true);
        off->setChecked(!anyOn);
        connect(off, &QAction::triggered, this, [this] { active()->setSubtitleTrack(0); });
        for (const MpvTrack& t : tracks) {
            QAction* a = ccMenu->addAction(t.label);
            a->setCheckable(true);
            a->setChecked(t.selected);
            const int id = t.id;
            connect(a, &QAction::triggered, this, [this, id] { active()->setSubtitleTrack(id); });
        }
        ccMenu->addSeparator();
        const QString target = QSettings(apppaths::configFile(), QSettings::IniFormat).value(QStringLiteral("subtitles/translateTo")).toString();
        QAction* tr8 = ccMenu->addAction(QString());
        tr8->setCheckable(true);
        if (target.isEmpty()) {
            tr8->setText(tr("Auto-translate (set a language in Settings)"));
            tr8->setEnabled(false);
        } else {
            tr8->setText(tr("Auto-translate to %1").arg(languageName(target)));
            tr8->setChecked(
                QSettings(apppaths::configFile(), QSettings::IniFormat).value(QStringLiteral("subtitles/translateEnabled"), false).toBool());
            connect(tr8, &QAction::toggled, this, [this](bool on) {
                QSettings(apppaths::configFile(), QSettings::IniFormat).setValue(QStringLiteral("subtitles/translateEnabled"), on);
                active()->reload();  // re-fetch captions with/without translation
            });
        }
    });

    // Playback speed selector.
    speedBtn_ = new QToolButton(transportBar_);
    speedBtn_->setText(QStringLiteral("1×"));
    speedBtn_->setToolTip(tr("Playback speed"));
    speedBtn_->setAutoRaise(true);
    speedBtn_->setFocusPolicy(Qt::NoFocus);
    speedBtn_->setPopupMode(QToolButton::InstantPopup);
    auto* speedMenu = new QMenu(speedBtn_);
    speedBtn_->setMenu(speedMenu);
    for (double s : {0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0}) {
        const QString label = s == 1.0 ? tr("Normal")
                                       : QStringLiteral("%1×").arg(s, 0, 'g', 3);
        QAction* a = speedMenu->addAction(label);
        a->setCheckable(true);
        a->setChecked(s == 1.0);
        connect(a, &QAction::triggered, this, [this, speedMenu, a, s, label] {
            video_->setSpeed(s);
            speedBtn_->setText(s == 1.0 ? QStringLiteral("1×") : label);
            for (QAction* other : speedMenu->actions()) {
                other->setChecked(other == a);
            }
        });
    }

    seek_ = new SeekSlider(Qt::Horizontal, transportBar_);
    seek_->setRange(0, 1000);
    seek_->setFocusPolicy(Qt::NoFocus);

    // Hover preview popup above the seek bar: a storyboard frame (when the
    // video has one) over a time code.
    spriteLoader_ = new ThumbnailLoader(this);
    preview_ = new QWidget(this);
    preview_->setVisible(false);
    preview_->setAttribute(Qt::WA_TransparentForMouseEvents);
    preview_->setStyleSheet(QStringLiteral("background:rgba(0,0,0,215);border-radius:6px;"));
    auto* previewCol = new QVBoxLayout(preview_);
    previewCol->setContentsMargins(4, 4, 4, 4);
    previewCol->setSpacing(3);
    previewThumb_ = new QLabel(preview_);
    previewThumb_->setFixedSize(160, 90);
    previewThumb_->setScaledContents(true);
    previewThumb_->setVisible(false);
    previewTime_ = new QLabel(preview_);
    previewTime_->setAlignment(Qt::AlignCenter);
    previewTime_->setStyleSheet(QStringLiteral("color:white;font-weight:bold;background:transparent;"));
    previewCol->addWidget(previewThumb_);
    previewCol->addWidget(previewTime_);

    // SponsorBlock overlays: a transient "Skipped … (Enter to revert)" toast and
    // a persistent "Press Enter to skip …" prompt for manual categories.
    const QString toastCss = QStringLiteral(
        "background:rgba(0,0,0,200);color:white;padding:6px 12px;border-radius:6px;");
    skipToast_ = new QLabel(this);
    skipToast_->setVisible(false);
    skipToast_->setAttribute(Qt::WA_TransparentForMouseEvents);
    skipToast_->setStyleSheet(toastCss);
    skipPrompt_ = new QLabel(this);
    skipPrompt_->setVisible(false);
    skipPrompt_->setAttribute(Qt::WA_TransparentForMouseEvents);
    skipPrompt_->setStyleSheet(toastCss);
    toastTimer_ = new QTimer(this);
    toastTimer_->setSingleShot(true);
    connect(toastTimer_, &QTimer::timeout, this, [this] {
        skipToast_->setVisible(false);
        pendingRevertIndex_ = -1;  // revert window closed
        // The toast overlays the GL video widget. While paused, that widget
        // isn't repainting, so just hiding the label leaves its stale pixels
        // composited on screen — force a repaint to erase them.
        if (video_) video_->update();
        if (pipVideo_) pipVideo_->update();
    });

    // "Resume from …" banner (shown in the ask-resume mode); click to jump.
    resumeBanner_ = new QPushButton(this);
    resumeBanner_->setVisible(false);
    resumeBanner_->setCursor(Qt::PointingHandCursor);
    resumeBanner_->setStyleSheet(QStringLiteral(
        "QPushButton{background:rgba(0,0,0,205);color:white;padding:7px 14px;"
        "border:none;border-radius:6px;} QPushButton:hover{background:rgba(40,40,40,230);}"));
    resumeTimer_ = new QTimer(this);
    resumeTimer_->setSingleShot(true);
    connect(resumeTimer_, &QTimer::timeout, this, [this] { resumeBanner_->setVisible(false); });
    connect(resumeBanner_, &QPushButton::clicked, this, [this] {
        video_->seekAbsolute(resumeTarget_);
        resumeBanner_->setVisible(false);
        resumeTimer_->stop();
    });

    // Mini-player control bar (play/pause · expand · close), hidden until mini.
    miniBar_ = new QWidget(this);
    miniBar_->setVisible(false);
    miniBar_->setAutoFillBackground(true);
    auto* miniLay = new QHBoxLayout(miniBar_);
    miniLay->setContentsMargins(6, 1, 6, 1);
    miniLay->setSpacing(2);
    auto miniButton = [this](const char* icon, const QString& tip) {
        auto* b = new QToolButton(miniBar_);
        b->setIcon(QIcon::fromTheme(QString::fromUtf8(icon)));
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        return b;
    };
    auto* miniPlay = miniButton("media-playback-pause", tr("Play/Pause"));
    connect(miniPlay, &QToolButton::clicked, video_, &MpvWidget::togglePause);
    connect(video_, &MpvWidget::pausedChanged, miniPlay, [miniPlay](bool p) {
        miniPlay->setIcon(QIcon::fromTheme(p ? QStringLiteral("media-playback-start")
                                             : QStringLiteral("media-playback-pause")));
    });
    auto* miniExpand = miniButton("view-fullscreen", tr("Expand"));
    connect(miniExpand, &QToolButton::clicked, this, &PlayerPage::expandRequested);
    auto* miniClose = miniButton("window-close", tr("Close"));
    connect(miniClose, &QToolButton::clicked, this, &PlayerPage::closeRequested);
    miniLay->addWidget(miniPlay);
    miniLay->addStretch();
    miniLay->addWidget(miniExpand);
    miniLay->addWidget(miniClose);

    // "Playing in Picture-in-Picture" overlay (shown over the empty player area
    // while the video lives in the PiP window).
    pipMessage_ = new QWidget(this);
    pipMessage_->setVisible(false);
    pipMessage_->setAutoFillBackground(true);
    auto* pm = new QVBoxLayout(pipMessage_);
    pm->addStretch();
    auto* pipLabel = new QLabel(tr("This video is playing in Picture-in-Picture."), pipMessage_);
    pipLabel->setAlignment(Qt::AlignCenter);
    auto* pipReturn = new QPushButton(QIcon::fromTheme(QStringLiteral("view-restore")),
                                      tr("Return to player"), pipMessage_);
    connect(pipReturn, &QPushButton::clicked, this, &PlayerPage::exitPip);
    pm->addWidget(pipLabel, 0, Qt::AlignCenter);
    pm->addSpacing(14);
    pm->addWidget(pipReturn, 0, Qt::AlignCenter);
    pm->addStretch();

    volume_ = new QSlider(Qt::Horizontal, transportBar_);
    volume_->setRange(0, 130);
    volume_->setValue(QSettings(apppaths::configFile(), QSettings::IniFormat).value(QStringLiteral("playback/volume"), 100).toInt());
    volume_->setMaximumWidth(110);
    volume_->setFocusPolicy(Qt::NoFocus);

    timeLabel_ = new QLabel(QStringLiteral("0:00 / 0:00"), transportBar_);

    // Current chapter name (shown while playing) + a chapter-jump menu.
    currentChapterLabel_ = new QLabel(transportBar_);
    currentChapterLabel_->setEnabled(false);  // muted
    currentChapterLabel_->setVisible(false);

    chaptersBtn_ = new QToolButton(transportBar_);
    chaptersBtn_->setIcon(QIcon::fromTheme(QStringLiteral("view-list-details"),
                                           QIcon::fromTheme(QStringLiteral("format-list-unordered"))));
    chaptersBtn_->setToolTip(tr("Chapters"));
    chaptersBtn_->setAutoRaise(true);
    chaptersBtn_->setFocusPolicy(Qt::NoFocus);
    chaptersBtn_->setPopupMode(QToolButton::InstantPopup);
    chaptersBtn_->setVisible(false);
    auto* chaptersMenu = new QMenu(chaptersBtn_);
    chaptersBtn_->setMenu(chaptersMenu);
    connect(chaptersMenu, &QMenu::aboutToShow, this, [this, chaptersMenu] {
        chaptersMenu->clear();
        for (const Chapter& c : chapters_) {
            QAction* a = chaptersMenu->addAction(
                QStringLiteral("%1   %2").arg(formatTime(c.startSeconds), c.title));
            const double t = c.startSeconds;
            connect(a, &QAction::triggered, this, [this, t] { video_->seekAbsolute(t); });
        }
    });

    quality_ = new QComboBox(transportBar_);
    quality_->setFocusPolicy(Qt::NoFocus);
    quality_->setToolTip(tr("Video quality"));
    for (const Quality& q : kQualities) {
        quality_->addItem(QString::fromUtf8(q.label), QString::fromUtf8(q.format));
    }
    const QString savedQuality =
        QSettings(apppaths::configFile(), QSettings::IniFormat).value(QStringLiteral("playback/quality"), QStringLiteral("Auto")).toString();
    quality_->setCurrentIndex(qMax(0, quality_->findText(savedQuality)));

    // Row 1: the seek bar spans the full width.
    transportCol->addWidget(seek_);
    // Row 2: controls — playback + time on the left, options on the right.
    row->addWidget(prevBtn_);
    row->addWidget(playBtn_);
    row->addWidget(nextBtn_);
    row->addWidget(muteBtn_);
    row->addWidget(volume_);
    row->addWidget(timeLabel_);
    row->addWidget(currentChapterLabel_);
    row->addStretch();
    row->addWidget(chaptersBtn_);
    row->addWidget(loopBtn_);
    row->addWidget(pipBtn_);
    row->addWidget(screenshotBtn_);
    row->addWidget(ccBtn_);
    row->addWidget(speedBtn_);
    row->addWidget(quality_);
    row->addWidget(fsBtn_);
    transportCol->addLayout(row);
    col->addWidget(transportBar_);
    col->addWidget(miniBar_);  // sits below the video in mini-player mode

    hideTimer_ = new QTimer(this);
    hideTimer_->setSingleShot(true);
    connect(hideTimer_, &QTimer::timeout, this, &PlayerPage::hideControls);

    // ---- wiring ----
    connect(backBtn, &QPushButton::clicked, this, &PlayerPage::backRequested);
    connect(infoBtn_, &QToolButton::clicked, this, &PlayerPage::toggleInfo);
    connect(queueBtn_, &QToolButton::clicked, this, &PlayerPage::toggleQueue);
    connect(prevBtn_, &QToolButton::clicked, this, &PlayerPage::playPrev);
    connect(nextBtn_, &QToolButton::clicked, this, &PlayerPage::playNext);
    connect(video_, &MpvWidget::endReached, this, &PlayerPage::onPlaybackEnded);
    connect(playBtn_, &QToolButton::clicked, this, [this] { active()->togglePause(); });
    connect(fsBtn_, &QToolButton::clicked, this, &PlayerPage::fullscreenToggleRequested);
    connect(ccBtn_, &QToolButton::clicked, this, [this] {
        // The lit state is driven solely by mpv's real subtitle state below, so
        // undo QToolButton's automatic toggle-on-click (avoids it sticking lit
        // when there are no captions or when toggling off).
        ccBtn_->setChecked(!ccBtn_->isChecked());
        active()->cycleSubtitles();
    });
    connect(video_, &MpvWidget::subtitlesActive, ccBtn_, &QToolButton::setChecked);
    connect(muteBtn_, &QToolButton::clicked, this,
            [this] { active()->setMuted(muteBtn_->toolTip() != tr("Unmute")); });

    // Volume: apply to mpv and remember it for next time.
    connect(volume_, &QSlider::valueChanged, this, [this](int v) {
        active()->setVolume(v);
        QSettings(apppaths::configFile(), QSettings::IniFormat).setValue(QStringLiteral("playback/volume"), v);
    });

    // Quality: remember the choice and reload at the current position.
    connect(quality_, &QComboBox::currentIndexChanged, this, [this] {
        QSettings(apppaths::configFile(), QSettings::IniFormat).setValue(QStringLiteral("playback/quality"), quality_->currentText());
        video_->setYtdlFormat(quality_->currentData().toString());
        video_->reload();
    });

    connect(seek_, &QSlider::sliderPressed, this, [this] { seeking_ = true; });
    connect(seek_, &QSlider::sliderReleased, this, [this] {
        seeking_ = false;
        if (duration_ > 0) {
            active()->seekAbsolute(duration_ * seek_->value() / 1000.0);
        }
    });

    // Preview the hovered time (and storyboard frame, if any) above the seek bar.
    connect(seek_, &SeekSlider::hovered, this, [this](double fraction, int xInSlider) {
        if (duration_ <= 0) {
            preview_->hide();
            return;
        }
        lastHoverX_ = xInSlider;
        const double t = fraction * duration_;
        const QString chapter = chapterTitleAt(t);
        previewTime_->setText(chapter.isEmpty()
                                  ? formatTime(t)
                                  : QStringLiteral("%1\n%2").arg(chapter, formatTime(t)));

        QString url;
        QRect rect;
        if (storyboardTile(t, url, rect)) {
            const QPixmap sprite = spriteLoader_->cached(url);
            if (!sprite.isNull()) {
                previewThumb_->setPixmap(sprite.copy(rect));
                previewThumb_->setVisible(true);
            } else {
                pendingSpriteUrl_ = url;
                pendingTileRect_ = rect;
                spriteLoader_->request(url);  // shown when it arrives
            }
        } else {
            previewThumb_->setVisible(false);
        }
        positionPreview();
        preview_->show();
        preview_->raise();
    });
    connect(seek_, &SeekSlider::hoverLeft, this, [this] { preview_->hide(); });
    // A sprite finished downloading — if it's the one we're waiting on, crop it.
    connect(spriteLoader_, &ThumbnailLoader::loaded, this,
            [this](const QString& url, const QPixmap& pm) {
                if (url == pendingSpriteUrl_ && !pm.isNull() && preview_->isVisible()) {
                    previewThumb_->setPixmap(pm.copy(pendingTileRect_));
                    previewThumb_->setVisible(true);
                    positionPreview();
                }
            });

    connect(video_, &MpvWidget::durationChanged, this, [this](double d) {
        duration_ = d;
        updateSponsorMarks();  // need the duration to place segments on the bar
    });
    connect(video_, &MpvWidget::positionChanged, this, [this](double pos) {
        position_ = pos;
        updateSponsorState(pos);
        // Persist resume position every few seconds (quietly).
        if (db_ && duration_ > 0 && !currentUrl_.isEmpty()) {
            const qint64 ip = static_cast<qint64>(pos);
            if (lastSavedPos_ < 0 || qAbs(ip - lastSavedPos_) >= 5) {
                lastSavedPos_ = ip;
                saveProgress(/*completed=*/false, /*notify=*/false);
            }
        }
        if (!seeking_ && duration_ > 0) {
            seek_->setValue(static_cast<int>(pos / duration_ * 1000.0));
        }
        timeLabel_->setText(QStringLiteral("%1 / %2")
                                .arg(formatTime(pos), formatTime(duration_)));
        if (!chapters_.isEmpty()) {
            const QString ch = chapterTitleAt(pos);
            currentChapterLabel_->setText(ch.isEmpty() ? QString()
                                                       : QStringLiteral("· %1").arg(ch));
        }
    });
    connect(video_, &MpvWidget::pausedChanged, this, [this](bool p) {
        paused_ = p;
        updatePlayPauseIcon(p);
    });
    connect(video_, &MpvWidget::mutedChanged, this, [this](bool m) {
        muteBtn_->setIcon(QIcon::fromTheme(m ? QStringLiteral("audio-volume-muted")
                                             : QStringLiteral("audio-volume-high")));
        muteBtn_->setToolTip(m ? tr("Unmute") : tr("Mute (M)"));
    });
    connect(video_, &MpvWidget::fileLoaded, this, [this](const QString& t) {
        if (!t.isEmpty()) {
            titleLabel_->setText(t);
        }
    });

    // Description / richer title from yt-dlp metadata.
    connect(extractor_, &Extractor::detailsFinished, this, [this](const VideoDetails& d) {
        if (!d.title.isEmpty()) {
            titleLabel_->setText(d.title);
        }
        // Backfill the history entry with real metadata — a pasted link is first
        // recorded with just the URL as its title. (addHistory keeps progress.)
        if (db_ && !currentUrl_.isEmpty() && !d.title.isEmpty() &&
            QSettings(apppaths::configFile(), QSettings::IniFormat).value(QStringLiteral("history/rememberWatch"), true).toBool()) {
            SearchResult enriched;
            enriched.url = currentUrl_;
            enriched.title = d.title;
            enriched.channel = d.channel;
            enriched.thumbnailUrl = d.thumbnailUrl;
            enriched.durationSeconds = d.durationSeconds;
            db_->addHistory(enriched);
        }
        storyboard_ = d.storyboard;  // for seek-bar frame previews
        chapters_ = d.chapters;
        QList<double> marks;
        if (d.durationSeconds > 0) {
            for (const Chapter& c : chapters_) {
                marks.push_back(c.startSeconds / d.durationSeconds);
            }
        }
        seek_->setChapterMarks(marks);
        const bool hasChapters = !chapters_.isEmpty();
        chaptersBtn_->setVisible(hasChapters);
        currentChapterLabel_->setVisible(hasChapters);
        channelUrl_ = d.channelUrl;
        const QString name = d.channel.toHtmlEscaped();
        // Channel under the title: a link when we know the channel URL.
        if (d.channel.isEmpty()) {
            channelLabel_->clear();
        } else if (channelUrl_.isEmpty()) {
            channelLabel_->setText(name);
        } else {
            channelLabel_->setText(QStringLiteral("<a href=\"#\">%1</a>").arg(name));
        }
        QString html;
        if (!d.channel.isEmpty()) {
            html += channelUrl_.isEmpty()
                        ? QStringLiteral("<p><b>%1</b></p>").arg(name)
                        : QStringLiteral("<p><b><a href=\"freeflume://channel\">%1</a></b></p>")
                              .arg(name);
        }
        const QString desc =
            d.description.isEmpty()
                ? tr("No description available.")
                : htmlutil::linkifyTimestamps(htmlutil::linkify(d.description));
        html += QStringLiteral("<p>%1</p>").arg(desc);
        infoText_->setHtml(html);
    });
    connect(extractor_, &Extractor::detailsFailed, this,
            [this](const QString&) { infoText_->setPlainText(tr("Could not load description.")); });
}

void PlayerPage::play(const SearchResult& item) {
    queue_ = {item};
    queueTitle_.clear();
    queueList_->setItems({});
    playIndex(0);
}

void PlayerPage::playQueue(const QList<SearchResult>& items, int startIndex,
                           const QString& title) {
    if (items.isEmpty()) {
        return;
    }
    queue_ = items;
    queueTitle_ = title;
    queueHeader_->setText(title.isEmpty() ? tr("Up Next") : title);
    {  // reflect the current setting (it may have changed in Settings)
        const QSignalBlocker block(autoplayToggle_);
        autoplayToggle_->setChecked(
            QSettings(apppaths::configFile(), QSettings::IniFormat).value(QStringLiteral("playback/autoplayNext"), true).toBool());
    }
    queueList_->setItems(items);
    playIndex(qBound(0, startIndex, items.size() - 1));
    // Reveal the Up Next panel so the rest of the playlist is visible at a glance.
    if (queue_.size() > 1) {
        queuePanel_->setVisible(true);
        queueBtn_->setChecked(true);
    }
}

void PlayerPage::extendQueue(const QList<SearchResult>& items, const QString& title) {
    if (items.size() <= queue_.size()) {
        return;  // nothing new to add
    }
    int newIndex = -1;
    for (int i = 0; i < items.size(); ++i) {
        if (items[i].url == currentUrl_) {
            newIndex = i;
            break;
        }
    }
    if (newIndex < 0) {
        return;  // the playing video isn't in this list — not our queue
    }
    queue_ = items;
    queueIndex_ = newIndex;
    if (!title.isEmpty()) {
        queueTitle_ = title;
        queueHeader_->setText(title);
    }
    queueList_->setItems(items);
    updateQueueUi();
    if (queuePanel_->isVisible()) {
        queueBtn_->setChecked(true);
    }
}

void PlayerPage::playIndex(int index) {
    if (index < 0 || index >= queue_.size()) {
        return;
    }
    saveProgress(/*completed=*/false, /*notify=*/true);  // remember the outgoing video
    queueIndex_ = index;
    const SearchResult item = queue_.at(index);
    lastSavedPos_ = -1;
    resumeBanner_->setVisible(false);
    resumeTimer_->stop();

    // Follow the preferred quality from Settings for each new video (the
    // in-player dropdown still allows a per-video override afterwards).
    const QString pref =
        QSettings(apppaths::configFile(), QSettings::IniFormat).value(QStringLiteral("playback/quality"), QStringLiteral("Auto")).toString();
    const int qi = quality_->findText(pref);
    if (qi >= 0) {
        const QSignalBlocker block(quality_);  // don't trigger a reload here
        quality_->setCurrentIndex(qi);
    }

    channelUrl_.clear();
    currentUrl_ = item.url;
    position_ = 0.0;
    channelLabel_->clear();
    storyboard_ = Storyboard{};
    chapters_.clear();
    segments_.clear();
    skippedSegments_.clear();
    pendingSkipIndex_ = -1;
    pendingRevertIndex_ = -1;
    seek_->setSegments({});
    skipToast_->setVisible(false);
    skipPrompt_->setVisible(false);
    seek_->setChapterMarks({});
    chaptersBtn_->setVisible(false);
    currentChapterLabel_->setVisible(false);
    currentChapterLabel_->clear();
    loopBtn_->setChecked(false);  // loop is per-video (toggled handler clears mpv)
    previewThumb_->setVisible(false);
    titleLabel_->setText(item.title.isEmpty() ? tr("Loading…") : item.title);
    infoText_->setPlainText(tr("Loading description…"));
    video_->setVolume(volume_->value());
    video_->setYtdlFormat(quality_->currentData().toString());

    // Resume behaviour from a previously-saved position.
    double startSec = 0.0;
    if (db_) {
        const WatchProgress wp = db_->progress(item.url);
        const bool resumable = wp.position > 5 && !wp.completed &&
                               (wp.duration <= 0 || wp.position < wp.duration - 15);
        if (resumable) {
            const QString mode = QSettings(apppaths::configFile(), QSettings::IniFormat)
                                     .value(QStringLiteral("playback/resumeMode"),
                                            QStringLiteral("resume"))
                                     .toString();
            if (mode == QLatin1String("resume")) {
                startSec = wp.position;
            } else if (mode == QLatin1String("ask")) {
                showResumeBanner(wp.position);
            }  // "start" → begin at 0
        }
    }
    video_->play(item.url, startSec);
    extractor_->fetchDetails(item.url);
    requestSponsorSegments();
    revealControls();
    setFocus();

    updateQueueUi();
    emit nowPlaying(item);
}

void PlayerPage::requestSponsorSegments() {
    const QStringList cats = sponsor::enabledCategories();
    const QString id = share::videoId(currentUrl_);
    if (cats.isEmpty() || id.isEmpty()) {
        return;
    }
    sponsor_->fetch(id, cats);
}

void PlayerPage::updateSponsorMarks() {
    if (duration_ <= 0 || segments_.isEmpty()) {
        seek_->setSegments({});
        return;
    }
    QList<SeekSegment> bars;
    for (const SponsorSegment& s : segments_) {
        SeekSegment v;
        v.start = s.start / duration_;
        v.end = s.end / duration_;
        v.color = sponsor::colorFor(s.category);
        bars.push_back(v);
    }
    seek_->setSegments(bars);
}

void PlayerPage::updateSponsorState(double pos) {
    if (segments_.isEmpty() || seeking_) {
        return;
    }
    int manualUnder = -1;
    for (int i = 0; i < segments_.size(); ++i) {
        const SponsorSegment& s = segments_.at(i);
        const bool inside = pos >= s.start && pos < s.end - 0.15;
        if (!inside || skippedSegments_.contains(i)) {
            continue;  // outside, or already handled — allow manual re-watch
        }
        const sponsor::Mode mode = sponsor::modeFor(s.category);
        if (mode == sponsor::Mode::Auto) {
            skippedSegments_.insert(i);
            video_->seekAbsolute(s.end);
            beginRevert(i);
            return;  // one action per tick
        }
        if (mode == sponsor::Mode::Manual) {
            manualUnder = i;  // offer a press-Enter prompt
        }
    }

    if (manualUnder != pendingSkipIndex_) {
        pendingSkipIndex_ = manualUnder;
        if (manualUnder >= 0) {
            // Persistent prompt while the playhead is inside the manual segment.
            skipPrompt_->setText(tr("Press Enter to skip %1")
                                     .arg(sponsor::labelFor(segments_.at(manualUnder).category)));
            skipPrompt_->adjustSize();
            skipPrompt_->move(video_->mapTo(this, QPoint(16, 16)));
            skipPrompt_->setVisible(true);
            skipPrompt_->raise();
        } else {
            skipPrompt_->setVisible(false);
        }
    }
}

void PlayerPage::beginRevert(int index) {
    pendingRevertIndex_ = index;
    skipPrompt_->setVisible(false);
    showSkipToast(tr("⏭  Skipped %1 — Enter to revert")
                      .arg(sponsor::labelFor(segments_.at(index).category)),
                  5000);  // revert window
}

void PlayerPage::onSponsorEnter() {
    if (pendingSkipIndex_ >= 0 && pendingSkipIndex_ < segments_.size()) {
        const int i = pendingSkipIndex_;
        skippedSegments_.insert(i);
        pendingSkipIndex_ = -1;
        skipPrompt_->setVisible(false);
        video_->seekAbsolute(segments_.at(i).end);
        beginRevert(i);  // offer to undo the manual skip too
        return;
    }
    if (pendingRevertIndex_ >= 0 && pendingRevertIndex_ < segments_.size()) {
        const int i = pendingRevertIndex_;
        pendingRevertIndex_ = -1;
        toastTimer_->stop();
        skipToast_->setVisible(false);
        // Stays in skippedSegments_, so it plays through without re-skipping.
        video_->seekAbsolute(segments_.at(i).start);
    }
}

void PlayerPage::showSkipToast(const QString& text, int hideAfterMs) {
    skipToast_->setText(text);
    skipToast_->adjustSize();
    skipToast_->move(video_->mapTo(this, QPoint(16, 16)));
    skipToast_->setVisible(true);
    skipToast_->raise();
    toastTimer_->start(hideAfterMs);  // auto-hide; SponsorBlock passes its revert window
}

void PlayerPage::playNext() {
    if (queueIndex_ + 1 < queue_.size()) {
        playIndex(queueIndex_ + 1);
    }
}

void PlayerPage::playPrev() {
    if (queueIndex_ > 0) {
        playIndex(queueIndex_ - 1);
    }
}

void PlayerPage::onPlaybackEnded() {
    saveProgress(/*completed=*/true, /*notify=*/true);  // mark as watched
    currentUrl_.clear();  // so advancing doesn't re-save it as not-completed
    lastSavedPos_ = -1;
    const bool autoplay =
        QSettings(apppaths::configFile(), QSettings::IniFormat).value(QStringLiteral("playback/autoplayNext"), true).toBool();
    if (autoplay) {
        playNext();  // no-op when this was the last item
    }
}

void PlayerPage::saveProgress(bool completed, bool notify) {
    if (!db_ || currentUrl_.isEmpty() || duration_ <= 0) {
        return;
    }
    db_->setProgress(currentUrl_, static_cast<qint64>(position_),
                     static_cast<qint64>(duration_), completed, notify);
}

void PlayerPage::showResumeBanner(qint64 positionSec) {
    resumeTarget_ = static_cast<double>(positionSec);
    resumeBanner_->setText(tr("▶  Resume from %1").arg(formatTime(resumeTarget_)));
    resumeBanner_->adjustSize();
    resumeBanner_->move(video_->mapTo(this, QPoint(16, 16)));
    resumeBanner_->setVisible(true);
    resumeBanner_->raise();
    resumeTimer_->start(12000);  // dismisses to "start from beginning"
}

void PlayerPage::updateQueueUi() {
    const bool hasQueue = queue_.size() > 1;
    queueBtn_->setVisible(hasQueue);
    prevBtn_->setVisible(hasQueue);
    nextBtn_->setVisible(hasQueue);
    prevBtn_->setEnabled(queueIndex_ > 0);
    nextBtn_->setEnabled(queueIndex_ + 1 < queue_.size());
    if (hasQueue) {
        queueList_->setCurrentRow(queueIndex_);
    } else if (queuePanel_->isVisible()) {
        queuePanel_->setVisible(false);
        queueBtn_->setChecked(false);
    }
}

void PlayerPage::stop() {
    if (pipActive_) {
        exitPip();  // bring the video back before tearing down
    }
    saveProgress(/*completed=*/false, /*notify=*/true);  // remember where we left off
    lastSavedPos_ = -1;
    resumeBanner_->setVisible(false);
    resumeTimer_->stop();
    video_->stop();
    titleLabel_->setText(tr("Nothing playing"));
    channelLabel_->clear();
    channelUrl_.clear();
    currentUrl_.clear();
    position_ = 0.0;
    queue_.clear();
    queueIndex_ = -1;
    queueTitle_.clear();
    queueList_->setItems({});
    updateQueueUi();
    storyboard_ = Storyboard{};
    chapters_.clear();
    segments_.clear();
    skippedSegments_.clear();
    pendingSkipIndex_ = -1;
    pendingRevertIndex_ = -1;
    seek_->setSegments({});
    skipToast_->setVisible(false);
    skipPrompt_->setVisible(false);
    seek_->setChapterMarks({});
    chaptersBtn_->setVisible(false);
    currentChapterLabel_->setVisible(false);
    currentChapterLabel_->clear();
    previewThumb_->setVisible(false);
    infoText_->clear();
    timeLabel_->setText(QStringLiteral("0:00 / 0:00"));
    seek_->setValue(0);
}

void PlayerPage::showVideoContextMenu(const QPoint& pos) {
    if (currentUrl_.isEmpty()) {
        return;
    }
    QMenu menu(this);
    menu.addAction(paused_ ? tr("Pla&y") : tr("&Pause"), video_, &MpvWidget::togglePause);
    menu.addAction(fullScreen_ ? tr("Exit &Fullscreen") : tr("&Fullscreen"), this,
                   &PlayerPage::fullscreenToggleRequested);
    if (!channelUrl_.isEmpty()) {
        const QString ch = channelUrl_;
        menu.addAction(tr("Open &Channel"), [this, ch] { emit channelRequested(ch); });
    }
    if (queueIndex_ >= 0 && queueIndex_ < queue_.size()) {
        playlistmenu::addSubmenu(&menu, db_, queue_.at(queueIndex_), this);
        downloadmenu::addSubmenu(&menu, downloads_, queue_.at(queueIndex_));
    }
    menu.addSeparator();
    share::addActions(&menu, currentUrl_, position_);  // includes link-at-time
    menu.exec(video_->mapToGlobal(pos));
}

void PlayerPage::setDatabase(Database* db) {
    db_ = db;
    queueList_->setDatabase(db);  // "Save to Playlist" in the Up Next list too
}

void PlayerPage::setDownloadManager(DownloadManager* m) {
    downloads_ = m;
    queueList_->setDownloadManager(m);  // "Download" in the Up Next list too
}

QString PlayerPage::formatTime(double seconds) const {
    if (seconds < 0 || seconds != seconds /* NaN */) {
        seconds = 0;
    }
    const int total = static_cast<int>(seconds);
    const int h = total / 3600, m = (total % 3600) / 60, s = total % 60;
    if (h > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

bool PlayerPage::storyboardTile(double seconds, QString& url, QRect& rect) const {
    const Storyboard& sb = storyboard_;
    if (!sb.valid() || seconds < 0 || duration_ <= 0) {
        return false;
    }
    const int perSprite = sb.rows * sb.columns;
    const double interval = sb.fragments.first().duration / perSprite;  // s per tile
    if (interval <= 0) {
        return false;
    }
    int idx = static_cast<int>(seconds / interval);
    const int maxIdx = static_cast<int>((duration_ - 0.001) / interval);
    idx = qBound(0, idx, qMax(0, maxIdx));
    int frag = idx / perSprite;
    frag = qBound(0, frag, sb.fragments.size() - 1);
    const int tile = idx % perSprite;
    const int row = tile / sb.columns;
    const int col = tile % sb.columns;
    url = sb.fragments.at(frag).url;
    rect = QRect(col * sb.tileWidth, row * sb.tileHeight, sb.tileWidth, sb.tileHeight);
    return true;
}

QString PlayerPage::chapterTitleAt(double seconds) const {
    QString title;
    for (const Chapter& c : chapters_) {
        if (c.startSeconds <= seconds) {
            title = c.title;
        } else {
            break;  // chapters are ordered by start time
        }
    }
    if (title.size() > 34) {
        title = title.left(33) + QChar(0x2026);  // ellipsis
    }
    return title;
}

void PlayerPage::positionPreview() {
    preview_->adjustSize();
    const QPoint base = seek_->mapTo(this, QPoint(0, 0));
    int px = base.x() + lastHoverX_ - preview_->width() / 2;
    px = qBound(0, px, qMax(0, width() - preview_->width()));
    const int py = base.y() - preview_->height() - 6;
    preview_->move(px, py);
}

void PlayerPage::updatePlayPauseIcon(bool paused) {
    playBtn_->setIcon(QIcon::fromTheme(paused ? QStringLiteral("media-playback-start")
                                              : QStringLiteral("media-playback-pause")));
}

void PlayerPage::toggleInfo() {
    infoPanel_->setVisible(!infoPanel_->isVisible());
    infoBtn_->setChecked(infoPanel_->isVisible());
}

void PlayerPage::toggleQueue() {
    queuePanel_->setVisible(!queuePanel_->isVisible());
    queueBtn_->setChecked(queuePanel_->isVisible());
}

void PlayerPage::setFullScreen(bool on) {
    // The window handles the actual fullscreen + chrome hiding; we never
    // reparent the video widget (that would recreate its GL/mpv context).
    fullScreen_ = on;
    fsBtn_->setIcon(QIcon::fromTheme(on ? QStringLiteral("view-restore")
                                        : QStringLiteral("view-fullscreen")));
    hamburgerBtn_->setVisible(!miniMode_ && !fullScreen_);  // hidden in fullscreen
    revealControls();  // also (re)arms or cancels the auto-hide timer
    setFocus();
}

void PlayerPage::setMiniMode(bool on) {
    if (miniMode_ != on) {
        miniMode_ = on;
        topBar_->setVisible(!on);
        transportBar_->setVisible(!on);
        miniBar_->setVisible(on);
        if (on) {
            infoPanel_->setVisible(false);
            queuePanel_->setVisible(false);
            infoBtn_->setChecked(false);
            queueBtn_->setChecked(false);
            hideTimer_->stop();
            unsetCursor();
            video_->unsetCursor();
        } else {
            revealControls();
        }
    }
    // Hamburger only in windowed full playback (not mini, not fullscreen).
    hamburgerBtn_->setVisible(!miniMode_ && !fullScreen_);
}

void PlayerPage::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (pipActive_ && pipMessage_ && split_) {
        pipMessage_->setGeometry(split_->geometry());  // cover only the video area
    }
}

int PlayerPage::miniBarHeight() const {
    return miniBar_ ? miniBar_->sizeHint().height() : 0;
}

MpvWidget* PlayerPage::active() const {
    return (pipActive_ && pipVideo_) ? pipVideo_ : video_;
}

void PlayerPage::takeScreenshot() {
    if (currentUrl_.isEmpty()) {
        return;
    }
    // Render the current frame at the video's native resolution (no window black
    // bars, subtitles included). Done via the render API on the GUI thread with
    // the GL context current — unlike mpv's VO screenshot path, which crashes.
    const QImage frame = active()->grabCurrentFrame();
    if (frame.isNull()) {
        showSkipToast(tr("⚠  Screenshot failed"));
        return;
    }

    // Folder + name: <screenshot folder>/<slug>/<slug>_<timecode>_<###>.<ext>
    // where <slug> is the sanitised "<video title>_<video id>".
    const QString id = share::videoId(currentUrl_);
    const QString rawName = titleLabel_->text().isEmpty() ? id : titleLabel_->text();
    auto slugify = [](QString s) {
        QString out;
        for (const QChar c : s) {
            if (c.isLetterOrNumber() || c == u'-' || c == u'_') {
                out += c;
            } else if (c.isSpace() || c == u'.') {
                out += u'_';  // collapse separators to underscores; drop the rest
            }
        }
        while (out.contains(QStringLiteral("__"))) {
            out.replace(QStringLiteral("__"), QStringLiteral("_"));
        }
        out = out.left(80);
        while (out.endsWith(u'_')) {
            out.chop(1);
        }
        return out.isEmpty() ? QStringLiteral("video") : out;
    };
    const QString slug = slugify(rawName + QStringLiteral("_") + id);

    QString base = QSettings(apppaths::configFile(), QSettings::IniFormat)
                       .value(QStringLiteral("screenshot/folder"),
                              QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
                                  + QStringLiteral("/FreeFlume"))
                       .toString();
    if (base.isEmpty()) {
        base = QDir::homePath() + QStringLiteral("/FreeFlume");
    }
    QDir dir(base + QStringLiteral("/") + slug);
    if (!dir.mkpath(QStringLiteral("."))) {
        showSkipToast(tr("⚠  Couldn't create the screenshots folder"));
        return;
    }

    // Map the format setting to an extension + encode quality. Fall back to PNG
    // if Qt has no writer for the chosen format on this system.
    const QString sel =
        QSettings(apppaths::configFile(), QSettings::IniFormat).value(QStringLiteral("screenshot/format"), QStringLiteral("png")).toString();
    QString fmt = QStringLiteral("png");
    int quality = -1;  // -1 = the writer's default
    if (sel == QLatin1String("jpg")) {
        fmt = QStringLiteral("jpg");
        quality = 95;
    } else if (sel == QLatin1String("jxl-lossless")) {
        fmt = QStringLiteral("jxl");
        quality = 100;  // KImageFormats maps 100 → lossless
    } else if (sel == QLatin1String("jxl-lossy")) {
        fmt = QStringLiteral("jxl");
        quality = 90;
    }
    if (!QImageWriter::supportedImageFormats().contains(fmt.toLatin1())) {
        fmt = QStringLiteral("png");  // e.g. no JPEG XL plugin installed
        quality = -1;
        showSkipToast(tr("⚠  %1 unsupported here — saved as PNG").arg(sel));
    }

    QString tc = formatTime(pipActive_ ? pipPos_ : position_);
    tc.replace(u':', u'-');  // colons aren't valid in filenames

    // Pick the next free 3-digit sequence for this timecode.
    const QString prefix = QStringLiteral("%1_%2_").arg(slug, tc);
    QString file;
    for (int n = 1; n < 1000; ++n) {
        const QString candidate = QStringLiteral("%1%2.%3")
                                      .arg(prefix, QString::number(n).rightJustified(3, u'0'), fmt);
        if (!dir.exists(candidate)) {
            file = candidate;
            break;
        }
    }
    if (file.isEmpty()) {
        return;
    }

    QImageWriter writer(dir.filePath(file), fmt.toLatin1());
    if (quality >= 0) {
        writer.setQuality(quality);
    }
    if (!writer.write(frame)) {
        showSkipToast(tr("⚠  Couldn't save: %1").arg(writer.errorString()));
        return;
    }
    showSkipToast(tr("📸  Saved %1").arg(file));
}

void PlayerPage::togglePip() {
    if (pipActive_) {
        exitPip();
    } else {
        enterPip();
    }
}

void PlayerPage::enterPip() {
    if (pipActive_ || currentUrl_.isEmpty()) {
        return;
    }
    if (fullScreen_) {
        emit fullscreenToggleRequested();  // PiP + fullscreen is jarring — drop out first
    }
    pipActive_ = true;
    const QString url = currentUrl_;
    const double startAt = position_;
    int wantSub = -1;  // carry the active caption track over to the PiP instance
    for (const MpvTrack& t : video_->subtitleTracks()) {
        if (t.selected) {
            wantSub = t.id;
            break;
        }
    }
    video_->setPaused(true);  // pause the in-app copy; resumed on return

    // A *fresh* player in its own window — built from scratch (never reparented),
    // so its GL/mpv render context is valid. Reparenting the live widget blanks
    // the video permanently, so we spin up a second mpv instance instead.
    pipWindow_ = new QWidget(nullptr, Qt::Window | Qt::WindowStaysOnTopHint);
    pipWindow_->setWindowTitle(tr("FreeFlume — Picture-in-Picture"));
    const QSize pipSize = QSettings(apppaths::configFile(), QSettings::IniFormat)
                              .value(QStringLiteral("playback/pipSize"), QSize(480, 270))
                              .toSize();
    pipWindow_->resize(pipSize.isValid() ? pipSize : QSize(480, 270));
    auto* lay = new QVBoxLayout(pipWindow_);
    lay->setContentsMargins(0, 0, 0, 0);
    pipVideo_ = new MpvWidget(pipWindow_);
    pipVideo_->setMouseTracking(true);
    lay->addWidget(pipVideo_);

    // Auto-hiding control strip overlaid on the bottom of the PiP video.
    pipControls_ = new QWidget(pipWindow_);
    pipControls_->setAutoFillBackground(true);
    pipControls_->setMouseTracking(true);
    auto* pcl = new QHBoxLayout(pipControls_);
    pcl->setContentsMargins(6, 2, 6, 2);
    pcl->setSpacing(2);
    auto pcBtn = [this](const char* icon, const QString& tip) {
        auto* b = new QToolButton(pipControls_);
        b->setIcon(QIcon::fromTheme(QString::fromLatin1(icon)));
        b->setToolTip(tip);
        b->setAutoRaise(true);
        return b;
    };
    pipPlayBtn_ = pcBtn("media-playback-pause", tr("Play/Pause"));
    connect(pipPlayBtn_, &QToolButton::clicked, this,
            [this] { if (pipVideo_) pipVideo_->togglePause(); });
    auto* pipCc = new QToolButton(pipControls_);
    pipCc->setText(QStringLiteral("CC"));
    pipCc->setCheckable(true);
    pipCc->setAutoRaise(true);
    pipCc->setToolTip(tr("Subtitles / CC — menu for tracks & translate"));
    pipCc->setPopupMode(QToolButton::MenuButtonPopup);
    QFont ccFont = pipCc->font();
    ccFont.setBold(true);
    pipCc->setFont(ccFont);
    connect(pipCc, &QToolButton::clicked, this, [this, pipCc] {
        pipCc->setChecked(!pipCc->isChecked());  // lit state comes from mpv below
        if (pipVideo_) pipVideo_->cycleSubtitles();
    });
    auto* pipCcMenu = new QMenu(pipCc);
    pipCc->setMenu(pipCcMenu);
    connect(pipCcMenu, &QMenu::aboutToHide, this, [this] { revealPipControls(); });
    connect(pipCcMenu, &QMenu::aboutToShow, this, [this, pipCcMenu] {
        if (pipHideTimer_) {
            pipHideTimer_->stop();  // keep the strip up while the menu is open
        }
        pipCcMenu->clear();
        if (!pipVideo_) {
            return;
        }
        const QList<MpvTrack> tracks = pipVideo_->subtitleTracks();
        bool anyOn = false;
        for (const MpvTrack& t : tracks) {
            anyOn = anyOn || t.selected;
        }
        QAction* off = pipCcMenu->addAction(tr("Off"));
        off->setCheckable(true);
        off->setChecked(!anyOn);
        connect(off, &QAction::triggered, this,
                [this] { if (pipVideo_) pipVideo_->setSubtitleTrack(0); });
        for (const MpvTrack& t : tracks) {
            QAction* a = pipCcMenu->addAction(t.label);
            a->setCheckable(true);
            a->setChecked(t.selected);
            const int id = t.id;
            connect(a, &QAction::triggered, this,
                    [this, id] { if (pipVideo_) pipVideo_->setSubtitleTrack(id); });
        }
        pipCcMenu->addSeparator();
        const QString target = QSettings(apppaths::configFile(), QSettings::IniFormat).value(QStringLiteral("subtitles/translateTo")).toString();
        QAction* tr8 = pipCcMenu->addAction(QString());
        tr8->setCheckable(true);
        if (target.isEmpty()) {
            tr8->setText(tr("Auto-translate (set a language in Settings)"));
            tr8->setEnabled(false);
        } else {
            tr8->setText(tr("Auto-translate to %1").arg(languageName(target)));
            tr8->setChecked(
                QSettings(apppaths::configFile(), QSettings::IniFormat).value(QStringLiteral("subtitles/translateEnabled"), false).toBool());
            connect(tr8, &QAction::toggled, this, [this](bool on) {
                QSettings(apppaths::configFile(), QSettings::IniFormat).setValue(QStringLiteral("subtitles/translateEnabled"), on);
                if (pipVideo_) pipVideo_->reload();  // re-fetch with/without translation
            });
        }
    });
    auto* pcReturn = pcBtn("view-restore", tr("Return to app"));
    connect(pcReturn, &QToolButton::clicked, this, &PlayerPage::exitPip);
    auto* pcClose = pcBtn("window-close", tr("Close"));
    connect(pcClose, &QToolButton::clicked, this,
            [this] { exitPip(); emit closeRequested(); });
    pcl->addWidget(pipPlayBtn_);
    pcl->addStretch();
    pcl->addWidget(pipCc);
    pcl->addWidget(pcReturn);
    pcl->addWidget(pcClose);
    connect(pipVideo_, &MpvWidget::subtitlesActive, pipCc, &QToolButton::setChecked);
    // One shared caption selection: mirror PiP's track back to the main player so
    // it's already correct on return (and lights the main CC button via video_).
    connect(pipVideo_, &MpvWidget::subtitlesActive, this, [this] {
        if (!pipVideo_) {
            return;
        }
        int sel = 0;
        for (const MpvTrack& t : pipVideo_->subtitleTracks()) {
            if (t.selected) {
                sel = t.id;
                break;
            }
        }
        video_->setSubtitleTrack(sel);
    });

    pipHideTimer_ = new QTimer(pipWindow_);
    pipHideTimer_->setInterval(2500);
    pipHideTimer_->setSingleShot(true);
    connect(pipHideTimer_, &QTimer::timeout, this, [this] {
        if (pipControls_) pipControls_->hide();
        if (pipWindow_) pipWindow_->setCursor(Qt::BlankCursor);
    });

    pipWindow_->installEventFilter(this);   // close / resize / mouse-move
    pipVideo_->installEventFilter(this);    // mouse-move over the video
    pipControls_->installEventFilter(this); // keep visible while hovered

    pipPos_ = startAt;
    // Mirror the PiP instance into the main transport bar (which now drives it).
    connect(pipVideo_, &MpvWidget::positionChanged, this, [this](double p) {
        pipPos_ = p;
        if (!seeking_ && duration_ > 0) {
            seek_->setValue(static_cast<int>(p / duration_ * 1000.0));
        }
        timeLabel_->setText(QStringLiteral("%1 / %2")
                                .arg(formatTime(p), formatTime(duration_)));
    });
    connect(pipVideo_, &MpvWidget::durationChanged, this,
            [this](double d) { duration_ = d; });
    if (wantSub > 0) {  // restore the caption track once the PiP video loads
        connect(pipVideo_, &MpvWidget::fileLoaded, this, [this, wantSub](const QString&) {
            if (pipVideo_) pipVideo_->setSubtitleTrack(wantSub);
        });
    }
    connect(pipVideo_, &MpvWidget::pausedChanged, this, [this](bool p) {
        updatePlayPauseIcon(p);
        if (pipPlayBtn_) {
            pipPlayBtn_->setIcon(QIcon::fromTheme(p ? QStringLiteral("media-playback-start")
                                                    : QStringLiteral("media-playback-pause")));
        }
    });

    pipVideo_->setVolume(volume_->value());
    pipVideo_->setYtdlFormat(quality_->currentData().toString());
    pipWindow_->show();
    pipVideo_->play(url, startAt);  // re-extracts; ~1–2s to start

    // Main window: keep the controls; the banner only covers the video area.
    pipMessage_->setGeometry(split_->geometry());
    pipMessage_->setVisible(true);
    pipMessage_->raise();
    revealPipControls();
}

void PlayerPage::revealPipControls() {
    if (!pipControls_ || !pipWindow_) {
        return;
    }
    const int h = pipControls_->sizeHint().height();
    pipControls_->setGeometry(0, pipWindow_->height() - h, pipWindow_->width(), h);
    pipControls_->show();
    pipControls_->raise();
    pipWindow_->unsetCursor();
    pipHideTimer_->start();
}

void PlayerPage::exitPip() {
    if (!pipActive_) {
        return;
    }
    pipActive_ = false;
    const double resumeAt = pipPos_;
    if (pipWindow_) {
        // Remember the window size so PiP reopens where the user left it.
        QSettings(apppaths::configFile(), QSettings::IniFormat)
            .setValue(QStringLiteral("playback/pipSize"), pipWindow_->size());
        pipWindow_->removeEventFilter(this);
        pipWindow_->hide();
        pipWindow_->deleteLater();  // also destroys pipVideo_ + its controls
        pipWindow_ = nullptr;
        pipVideo_ = nullptr;
        pipControls_ = nullptr;
        pipPlayBtn_ = nullptr;
        pipHideTimer_ = nullptr;
    }
    pipMessage_->setVisible(false);
    // Resume the in-app player where PiP left off.
    if (resumeAt > 0) {
        video_->seekAbsolute(resumeAt);
    }
    video_->setPaused(false);
    setFocus();
}

void PlayerPage::revealControls() {
    if (miniMode_) {
        return;  // the mini-player has its own compact bar
    }
    topBar_->show();
    transportBar_->show();
    unsetCursor();
    video_->unsetCursor();
    if (fullScreen_) {
        hideTimer_->start(kAutoHideMs);  // hide again after inactivity
    } else {
        hideTimer_->stop();  // always-visible when windowed
    }
}

void PlayerPage::hideControls() {
    if (!fullScreen_) {
        return;
    }
    // Don't hide while the user is hovering the controls — try again later.
    if (topBar_->underMouse() || transportBar_->underMouse() || infoPanel_->underMouse()) {
        hideTimer_->start(kAutoHideMs);
        return;
    }
    topBar_->hide();
    transportBar_->hide();
    setCursor(Qt::BlankCursor);
    video_->setCursor(Qt::BlankCursor);
}

void PlayerPage::mouseMoveEvent(QMouseEvent* event) {
    revealControls();
    QWidget::mouseMoveEvent(event);
}

bool PlayerPage::eventFilter(QObject* watched, QEvent* event) {
    if (watched == pipWindow_) {
        if (event->type() == QEvent::Close) {
            exitPip();  // closing the PiP window brings the video back to the app
            return true;
        }
        if (event->type() == QEvent::Resize || event->type() == QEvent::MouseMove) {
            revealPipControls();
        }
        return QWidget::eventFilter(watched, event);
    }
    if (watched == pipVideo_ || watched == pipControls_) {
        if (event->type() == QEvent::MouseMove) {
            revealPipControls();
        }
        return QWidget::eventFilter(watched, event);
    }
    if (watched == video_) {
        if (event->type() == QEvent::KeyPress) {
            keyPressEvent(static_cast<QKeyEvent*>(event));
            return true;
        }
        if (pipActive_) {
            return QWidget::eventFilter(watched, event);  // no app UI from the PiP video
        }
        if (miniMode_) {
            // In the mini-player a click anywhere on the video expands it.
            if (event->type() == QEvent::MouseButtonRelease &&
                static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
                emit expandRequested();
                return true;
            }
            return QWidget::eventFilter(watched, event);
        }
        if (event->type() == QEvent::MouseMove) {
            revealControls();
        }
        if (event->type() == QEvent::MouseButtonDblClick &&
            static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
            emit fullscreenToggleRequested();  // double-click toggles fullscreen
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void PlayerPage::performAction(const QString& id) {
    if (id == QLatin1String("playPause")) {
        active()->togglePause();
    } else if (id == QLatin1String("seekBack")) {
        active()->seekRelative(-5);
    } else if (id == QLatin1String("seekForward")) {
        active()->seekRelative(5);
    } else if (id == QLatin1String("prevFrame")) {
        active()->frameStep(-1);
    } else if (id == QLatin1String("nextFrame")) {
        active()->frameStep(1);
    } else if (id == QLatin1String("volUp")) {
        volume_->setValue(volume_->value() + 5);
    } else if (id == QLatin1String("volDown")) {
        volume_->setValue(volume_->value() - 5);
    } else if (id == QLatin1String("mute")) {
        muteBtn_->click();
    } else if (id == QLatin1String("subtitles")) {
        active()->cycleSubtitles();
    } else if (id == QLatin1String("fullscreen")) {
        emit fullscreenToggleRequested();
    } else if (id == QLatin1String("loop")) {
        loopBtn_->toggle();
    } else if (id == QLatin1String("nextVideo")) {
        playNext();
    } else if (id == QLatin1String("prevVideo")) {
        playPrev();
    } else if (id == QLatin1String("screenshot")) {
        takeScreenshot();
    } else if (id == QLatin1String("info")) {
        toggleInfo();
    } else if (id == QLatin1String("queue")) {
        if (queue_.size() > 1) {
            toggleQueue();
        }
    }
}

void PlayerPage::keyPressEvent(QKeyEvent* event) {
    // Enter (SponsorBlock) and Escape (back / leave fullscreen) are contextual and
    // not user-rebindable.
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        onSponsorEnter();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        if (fullScreen_) {
            emit fullscreenToggleRequested();
        } else {
            emit backRequested();
        }
        return;
    }

    // Match the pressed key against the configured shortcuts (compared as combined
    // integer key codes — QKeySequence text round-trips break on keys like ",").
    const Qt::KeyboardModifiers mods =
        event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier |
                              Qt::MetaModifier);
    const int pressed = QKeyCombination(mods, static_cast<Qt::Key>(event->key())).toCombined();
    for (const shortcuts::Action& a : shortcuts::actions()) {
        if (shortcuts::keyFor(a.id) == pressed) {
            performAction(a.id);
            return;
        }
    }
    QWidget::keyPressEvent(event);
}
