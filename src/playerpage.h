// FreeFlume — player page (video + transport controls + info panel).
#pragma once

#include <QRect>
#include <QSet>
#include <QString>
#include <QWidget>

#include "extractor.h"
#include "sponsorblock.h"

class Database;
class DownloadManager;
class Extractor;
class MpvWidget;
class SponsorBlock;
class SeekSlider;
class ThumbnailLoader;
class VideoList;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSplitter;
class QSlider;
class QTimer;
class QToolButton;
class QTextBrowser;

class PlayerPage : public QWidget {
    Q_OBJECT

public:
    explicit PlayerPage(QWidget* parent = nullptr);

    void setDatabase(Database* db);  // enables "Save to Playlist" in the menus
    void setDownloadManager(DownloadManager* m);  // enables "Download" in the menus

    void play(const SearchResult& item);  // play a single video (clears the queue)
    // Play a list of videos as a queue, starting at startIndex; title labels the
    // "Up Next" panel (e.g. the playlist name).
    void playQueue(const QList<SearchResult>& items, int startIndex, const QString& title);
    // Replace the current queue with a fuller list (e.g. the whole playlist that
    // finished loading) without interrupting playback. No-op if the currently
    // playing video isn't part of `items`.
    void extendQueue(const QList<SearchResult>& items, const QString& title);
    void stop();  // stop playback (e.g. when navigating away from the player)

    // Called by the window after it enters/leaves fullscreen, so the page can
    // update its button state, Esc handling, and control auto-hide.
    void setFullScreen(bool on);

    // Mini-player: hide the full chrome and show a compact overlay bar.
    void setMiniMode(bool on);
    bool isPlaying() const { return !currentUrl_.isEmpty(); }
    int miniBarHeight() const;  // height of the mini control strip below the video

    // True picture-in-picture: pop the video into a separate floating window.
    void togglePip();
    bool pipActive() const { return pipActive_; }

signals:
    void backRequested();
    void fullscreenToggleRequested();
    void channelRequested(const QString& channelUrl);  // clicked the channel name
    void nowPlaying(const SearchResult& item);  // a video started (for history)
    void expandRequested();  // mini-player → full
    void closeRequested();   // mini-player × → stop & hide
    void toggleSidebarRequested();  // hamburger → show/hide the main sidebar

protected:
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QString formatTime(double seconds) const;
    void updatePlayPauseIcon(bool paused);
    void toggleInfo();
    void toggleQueue();
    void playIndex(int index);   // play queue_[index]
    void playNext();
    void playPrev();
    void onPlaybackEnded();      // mpv reached natural end-of-file
    void saveProgress(bool completed, bool notify);  // persist resume position
    void showResumeBanner(qint64 positionSec);       // "ask" mode prompt
    void updateQueueUi();        // refresh prev/next/queue-button state + highlight
    void requestSponsorSegments();             // fetch for the current video
    void updateSponsorMarks();                 // recolour the seek bar
    void updateSponsorState(double pos);       // auto-skip / show the manual prompt
    void beginRevert(int index);               // offer to undo a skip (Enter)
    void onSponsorEnter();                      // Enter: manual-skip or revert
    void showSkipToast(const QString& text);
    void revealControls();
    void hideControls();
    void showVideoContextMenu(const QPoint& pos);
    bool storyboardTile(double seconds, QString& url, QRect& rect) const;
    QString chapterTitleAt(double seconds) const;
    void positionPreview();

    void enterPip();
    void exitPip();
    void takeScreenshot();  // save the current frame to Pictures/FreeFlume/…
    void performAction(const QString& id);  // run a bindable shortcut action
    void revealPipControls();  // show the PiP strip and restart its hide timer
    MpvWidget* active() const;  // the instance the transport bar drives (PiP or main)

    Extractor* extractor_ = nullptr;  // owned — fetches title/description
    Database* db_ = nullptr;          // not owned — for "Save to Playlist"
    DownloadManager* downloads_ = nullptr;  // not owned — for "Download"
    SponsorBlock* sponsor_ = nullptr;  // owned — fetches skippable segments
    MpvWidget* video_ = nullptr;
    QSplitter* split_ = nullptr;      // holds the video
    QWidget* pipWindow_ = nullptr;    // separate floating window for PiP
    MpvWidget* pipVideo_ = nullptr;   // its own player (a fresh mpv instance)
    QWidget* pipControls_ = nullptr;  // auto-hiding control strip over the PiP video
    QToolButton* pipPlayBtn_ = nullptr;
    QTimer* pipHideTimer_ = nullptr;  // hides the PiP controls after inactivity
    QWidget* pipMessage_ = nullptr;   // "playing in PiP" banner over the video area
    QToolButton* pipBtn_ = nullptr;
    double pipPos_ = 0.0;             // PiP playback position, to resume on return
    bool pipActive_ = false;
    QWidget* topBar_ = nullptr;
    QToolButton* hamburgerBtn_ = nullptr;  // shown only in windowed full playback
    QWidget* transportBar_ = nullptr;
    QWidget* miniBar_ = nullptr;  // compact controls shown in mini-player mode
    bool miniMode_ = false;
    QWidget* infoPanel_ = nullptr;
    QTextBrowser* infoText_ = nullptr;
    QWidget* queuePanel_ = nullptr;   // "Up Next" list panel
    QLabel* queueHeader_ = nullptr;   // playlist title above the queue list
    QCheckBox* autoplayToggle_ = nullptr;  // mirrors playback/autoplayNext
    VideoList* queueList_ = nullptr;
    ThumbnailLoader* queueThumbs_ = nullptr;
    QLabel* titleLabel_ = nullptr;
    QLabel* channelLabel_ = nullptr;
    QLabel* timeLabel_ = nullptr;
    QLabel* currentChapterLabel_ = nullptr;
    QToolButton* chaptersBtn_ = nullptr;
    QToolButton* playBtn_ = nullptr;
    QToolButton* prevBtn_ = nullptr;
    QToolButton* nextBtn_ = nullptr;
    QToolButton* muteBtn_ = nullptr;
    QToolButton* fsBtn_ = nullptr;
    QToolButton* infoBtn_ = nullptr;
    QToolButton* queueBtn_ = nullptr;
    QToolButton* ccBtn_ = nullptr;
    QToolButton* audioBtn_ = nullptr;
    QToolButton* speedBtn_ = nullptr;
    QToolButton* loopBtn_ = nullptr;
    QToolButton* screenshotBtn_ = nullptr;
    SeekSlider* seek_ = nullptr;
    QSlider* volume_ = nullptr;
    QComboBox* quality_ = nullptr;
    QWidget* preview_ = nullptr;      // hover preview popup above the seek bar
    QLabel* previewThumb_ = nullptr;  // storyboard frame (when available)
    QLabel* previewTime_ = nullptr;   // time code
    ThumbnailLoader* spriteLoader_ = nullptr;
    QTimer* hideTimer_ = nullptr;

    QLabel* skipToast_ = nullptr;   // transient "Skipped … (Enter to revert)" overlay
    QLabel* skipPrompt_ = nullptr;  // persistent "Press Enter to skip …" prompt
    QPushButton* resumeBanner_ = nullptr;  // "Resume from …" (ask mode)
    QTimer* toastTimer_ = nullptr;
    QTimer* resumeTimer_ = nullptr;
    qint64 lastSavedPos_ = -1;     // throttles progress writes
    double resumeTarget_ = 0.0;    // position the resume banner seeks to
    QList<SponsorSegment> segments_;  // SponsorBlock segments for the current video
    QSet<int> skippedSegments_;       // indices already skipped this load
    int pendingSkipIndex_ = -1;       // manual segment under the playhead (Enter skips)
    int pendingRevertIndex_ = -1;     // recently skipped segment (Enter reverts)

    QString channelUrl_;  // channel of the current video (for the clickable name)
    QString currentUrl_;  // URL of the current video (for share/context menu)
    double position_ = 0.0;  // last reported playback position, seconds
    QList<SearchResult> queue_;  // current play queue (1 item for single plays)
    int queueIndex_ = -1;        // index of the playing item within queue_
    QString queueTitle_;         // playlist/context name for the Up Next panel
    Storyboard storyboard_;
    QList<Chapter> chapters_;
    QString pendingSpriteUrl_;
    QRect pendingTileRect_;
    int lastHoverX_ = 0;
    double duration_ = 0.0;
    bool seeking_ = false;
    bool paused_ = false;
    bool fullScreen_ = false;
};
