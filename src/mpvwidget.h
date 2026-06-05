// FreeFlume — embedded libmpv video widget.
//
// Renders libmpv into a QOpenGLWidget using mpv's OpenGL render API. This is
// the portable, Wayland-safe approach (no native window-handle embedding).
#pragma once

#include <QList>
#include <QOpenGLWidget>
#include <QString>

#include "idleinhibitor.h"

class QImage;

struct mpv_handle;
struct mpv_render_context;
struct mpv_event;

// A selectable media track (audio/subtitle).
struct MpvTrack {
    int id = 0;
    QString label;
    bool selected = false;
};

class MpvWidget : public QOpenGLWidget {
    Q_OBJECT

public:
    explicit MpvWidget(QWidget* parent = nullptr);
    ~MpvWidget() override;

    // High-level controls. startSeconds resumes playback at that offset.
    void play(const QString& url, double startSeconds = 0.0);
    void stop();
    void togglePause();
    void setPaused(bool paused);
    void seekRelative(double seconds);
    void seekAbsolute(double seconds);
    void setVolume(int percent);
    void setMuted(bool muted);
    void setSpeed(double speed);  // playback rate (1.0 = normal)
    void setLoop(bool on);        // repeat the current file
    void frameStep(int direction);  // step one frame forward (>=0) or back (<0); pauses

    // Quality: set the yt-dlp format string, and reload the current video at
    // the same position so a new quality takes effect mid-playback.
    void setYtdlFormat(const QString& format);
    void reload();

    // Captions: cycle through subtitle tracks (off → track 1 → … → off).
    void cycleSubtitles();

    // Capture the current frame at the video's native resolution (with visible
    // subtitles, no black bars), rendered offscreen via the render API. Returns a
    // null image if nothing is playing.
    QImage grabCurrentFrame();

    // Subtitle tracks: enumerate and select (by track id; id <= 0 = off).
    QList<MpvTrack> subtitleTracks() const;
    void setSubtitleTrack(int id);

signals:
    void positionChanged(double seconds);
    void durationChanged(double seconds);
    void pausedChanged(bool paused);
    void mutedChanged(bool muted);
    void subtitlesActive(bool on);
    void fileLoaded(const QString& title);
    void endReached();

protected:
    void initializeGL() override;
    void paintGL() override;

private slots:
    void drainEvents();   // runs on the GUI thread
    void doUpdate();      // schedule a repaint (GUI thread)

private:
    void command(const QStringList& args);
    void setOption(const QString& name, const QString& value);
    QList<MpvTrack> tracksOfType(const char* type) const;
    void applySubtitleSettings();  // reads QSettings → mpv subtitle options
    void handleEvent(mpv_event* event);

    // libmpv callbacks (may fire on arbitrary threads).
    static void onWakeup(void* ctx);
    static void onRenderUpdate(void* ctx);

    mpv_handle* mpv_ = nullptr;
    mpv_render_context* renderCtx_ = nullptr;
    QString currentUrl_;
    double pendingSeek_ = 0.0;  // seek-to position applied on the next load
    IdleInhibitor inhibitor_;   // blocks screensaver/suspend while playing
};
