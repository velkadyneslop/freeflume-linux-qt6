// FreeFlume — embedded libmpv video widget implementation.
#include "apppaths.h"
#include "mpvwidget.h"

#include <clocale>
#include <stdexcept>

#include <QByteArray>
#include <QImage>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QSettings>
#include <QStringList>

#include <mpv/client.h>
#include <mpv/render_gl.h>

namespace {

// Resolves OpenGL function pointers via the current Qt GL context.
void* getProcAddress(void* /*ctx*/, const char* name) {
    QOpenGLContext* glctx = QOpenGLContext::currentContext();
    if (!glctx) {
        return nullptr;
    }
    return reinterpret_cast<void*>(glctx->getProcAddress(QByteArray(name)));
}

}  // namespace

MpvWidget::MpvWidget(QWidget* parent) : QOpenGLWidget(parent) {
    // libmpv requires the C locale for numeric parsing.
    std::setlocale(LC_NUMERIC, "C");

    mpv_ = mpv_create();
    if (!mpv_) {
        throw std::runtime_error("Could not create mpv context");
    }

    // Use yt-dlp for streaming URLs (YT, Rumble, BitChute, …).
    mpv_set_option_string(mpv_, "ytdl", "yes");
    mpv_set_option_string(mpv_, "script-opts", "ytdl_hook-ytdl_path=yt-dlp");
    // Captions: which to fetch + styling are applied from QSettings per video
    // (see applySubtitleSettings). Don't auto-show them on load.
    mpv_set_option_string(mpv_, "sub-auto", "no");
    mpv_set_option_string(mpv_, "vo", "libmpv");
    // Hardware decoding. We render through the libmpv GL render API into a
    // QOpenGLWidget, whose context can't do zero-copy GPU interop on every
    // platform (e.g. GLX/XWayland), and there mpv silently falls back to
    // software — which chugs on 4K/1440p AV1/VP9. "auto-copy" decodes on the
    // GPU and copies frames back to RAM for upload, so it engages HW decode
    // regardless of interop (the copy is cheap next to software AV1). Power
    // users can override with an explicit mpv mode (e.g. "vaapi" for zero-copy)
    // by setting playback/hwdec to that string in the config.
    const QString hwv = QSettings(apppaths::configFile(), QSettings::IniFormat)
                            .value(QStringLiteral("playback/hwdec"), QStringLiteral("true"))
                            .toString().trimmed();
    QString hwdec;
    if (hwv.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0 || hwv == QLatin1String("1")) {
        hwdec = QStringLiteral("auto-copy");
    } else if (hwv.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0 || hwv == QLatin1String("0")) {
        hwdec = QStringLiteral("no");
    } else {
        hwdec = hwv;  // explicit mpv hwdec mode from the config
    }
    mpv_set_option_string(mpv_, "hwdec", hwdec.toUtf8().constData());
    mpv_set_option_string(mpv_, "keep-open", "yes");

    if (mpv_initialize(mpv_) < 0) {
        throw std::runtime_error("Could not initialize mpv");
    }

    // Observe playback state for the UI.
    mpv_observe_property(mpv_, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv_, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv_, 0, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(mpv_, 0, "mute", MPV_FORMAT_FLAG);
    mpv_observe_property(mpv_, 0, "media-title", MPV_FORMAT_STRING);
    mpv_observe_property(mpv_, 0, "sid", MPV_FORMAT_STRING);
    mpv_observe_property(mpv_, 0, "eof-reached", MPV_FORMAT_FLAG);

    // Marshal mpv's wakeup (any thread) onto the GUI thread.
    mpv_set_wakeup_callback(mpv_, &MpvWidget::onWakeup, this);
}

MpvWidget::~MpvWidget() {
    makeCurrent();
    if (renderCtx_) {
        mpv_render_context_free(renderCtx_);
        renderCtx_ = nullptr;
    }
    if (mpv_) {
        mpv_destroy(mpv_);
        mpv_ = nullptr;
    }
    doneCurrent();
}

void MpvWidget::initializeGL() {
    // If the GL context was recreated (e.g. the widget was reparented), the old
    // render context is tied to the destroyed context — drop it before making a
    // new one. mpv only allows one render context per handle.
    if (renderCtx_) {
        mpv_render_context_free(renderCtx_);
        renderCtx_ = nullptr;
    }

    mpv_opengl_init_params glInit{getProcAddress, nullptr};
    int advanced = 1;
    mpv_render_param params[]{
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInit},
        {MPV_RENDER_PARAM_ADVANCED_CONTROL, &advanced},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };

    // Never throw from a GL callback — that would call std::terminate. Log and
    // leave renderCtx_ null; paintGL() guards against that.
    if (mpv_render_context_create(&renderCtx_, mpv_, params) < 0) {
        renderCtx_ = nullptr;
        qWarning("MpvWidget: failed to create mpv render context");
        return;
    }
    mpv_render_context_set_update_callback(renderCtx_, &MpvWidget::onRenderUpdate, this);
}

void MpvWidget::paintGL() {
    if (!renderCtx_) {
        return;
    }
    const qreal dpr = devicePixelRatioF();
    mpv_opengl_fbo fbo{static_cast<int>(defaultFramebufferObject()),
                       static_cast<int>(width() * dpr),
                       static_cast<int>(height() * dpr), 0};
    int flipY = 1;
    mpv_render_param params[]{
        {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flipY},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    mpv_render_context_render(renderCtx_, params);
}

// ---- Threading bridges -----------------------------------------------------

void MpvWidget::onWakeup(void* ctx) {
    auto* self = static_cast<MpvWidget*>(ctx);
    QMetaObject::invokeMethod(self, "drainEvents", Qt::QueuedConnection);
}

void MpvWidget::onRenderUpdate(void* ctx) {
    auto* self = static_cast<MpvWidget*>(ctx);
    QMetaObject::invokeMethod(self, "doUpdate", Qt::QueuedConnection);
}

void MpvWidget::doUpdate() {
    if (renderCtx_ &&
        (mpv_render_context_update(renderCtx_) & MPV_RENDER_UPDATE_FRAME)) {
        update();
    }
}

void MpvWidget::drainEvents() {
    while (mpv_) {
        mpv_event* event = mpv_wait_event(mpv_, 0);
        if (event->event_id == MPV_EVENT_NONE) {
            break;
        }
        handleEvent(event);
    }
}

void MpvWidget::handleEvent(mpv_event* event) {
    switch (event->event_id) {
        case MPV_EVENT_PROPERTY_CHANGE: {
            auto* prop = static_cast<mpv_event_property*>(event->data);
            const QString name = QString::fromUtf8(prop->name);
            if (prop->format == MPV_FORMAT_DOUBLE) {
                const double value = *static_cast<double*>(prop->data);
                if (name == QLatin1String("time-pos")) {
                    emit positionChanged(value);
                } else if (name == QLatin1String("duration")) {
                    emit durationChanged(value);
                }
            } else if (prop->format == MPV_FORMAT_FLAG) {
                const bool flag = *static_cast<int*>(prop->data) != 0;
                if (name == QLatin1String("pause")) {
                    inhibitor_.setActive(!flag);  // stay awake only while playing
                    emit pausedChanged(flag);
                } else if (name == QLatin1String("mute")) {
                    emit mutedChanged(flag);
                } else if (name == QLatin1String("eof-reached") && flag) {
                    // With keep-open=yes, mpv pauses at the end instead of
                    // emitting END_FILE, so this property is the end-of-video
                    // signal. It flips back to false on a new file or a seek.
                    inhibitor_.setActive(false);
                    emit endReached();
                }
            } else if (prop->format == MPV_FORMAT_STRING) {
                const QString value = QString::fromUtf8(*static_cast<char**>(prop->data));
                if (name == QLatin1String("media-title")) {
                    emit fileLoaded(value);
                } else if (name == QLatin1String("sid")) {
                    emit subtitlesActive(value != QLatin1String("no") &&
                                         value != QLatin1String("auto"));
                }
            }
            break;
        }
        case MPV_EVENT_FILE_LOADED:
            if (pendingSeek_ > 0) {
                seekAbsolute(pendingSeek_);
                pendingSeek_ = 0.0;
            }
            break;
        // With keep-open=yes the natural end is signalled by the eof-reached
        // property above, so END_FILE here only fires for stop/error/switch —
        // which must NOT advance the playlist.
        default:
            break;
    }
}

// ---- Commands --------------------------------------------------------------

void MpvWidget::command(const QStringList& args) {
    if (!mpv_) {
        return;
    }
    QList<QByteArray> bytes;
    bytes.reserve(args.size());
    for (const QString& a : args) {
        bytes.push_back(a.toUtf8());
    }
    std::vector<const char*> argv;
    argv.reserve(bytes.size() + 1);
    for (const QByteArray& b : bytes) {
        argv.push_back(b.constData());
    }
    argv.push_back(nullptr);
    mpv_command(mpv_, argv.data());
}

QImage MpvWidget::grabCurrentFrame() {
    if (!mpv_ || !renderCtx_) {
        return {};
    }
    int64_t vw = 0;
    int64_t vh = 0;
    if (mpv_get_property(mpv_, "dwidth", MPV_FORMAT_INT64, &vw) < 0 ||
        mpv_get_property(mpv_, "dheight", MPV_FORMAT_INT64, &vh) < 0 || vw <= 0 || vh <= 0) {
        return {};
    }

    makeCurrent();
    QOpenGLFramebufferObject fbo(static_cast<int>(vw), static_cast<int>(vh));
    fbo.bind();
    mpv_opengl_fbo mpvFbo{static_cast<int>(fbo.handle()), static_cast<int>(vw),
                          static_cast<int>(vh), 0};
    int flipY = 1;  // same orientation paintGL() uses
    mpv_render_param params[]{
        {MPV_RENDER_PARAM_OPENGL_FBO, &mpvFbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flipY},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    mpv_render_context_render(renderCtx_, params);
    QImage img = fbo.toImage();
    fbo.release();
    doneCurrent();
    // Drop the alpha channel so screenshots are always opaque.
    return img.convertToFormat(QImage::Format_RGB888);
}

void MpvWidget::setOption(const QString& name, const QString& value) {
    if (mpv_) {
        mpv_set_property_string(mpv_, name.toUtf8().constData(), value.toUtf8().constData());
    }
}

void MpvWidget::play(const QString& url, double startSeconds) {
    currentUrl_ = url;
    pendingSeek_ = startSeconds;  // applied once the file is loaded
    applySubtitleSettings();  // fetch/style captions per the latest settings
    command({QStringLiteral("loadfile"), url});
    setPaused(false);
}

void MpvWidget::setYtdlFormat(const QString& format) {
    if (mpv_ && !format.isEmpty()) {
        mpv_set_property_string(mpv_, "ytdl-format", format.toUtf8().constData());
    }
}

void MpvWidget::reload() {
    if (!mpv_ || currentUrl_.isEmpty()) {
        return;
    }
    // Reload at the current position so changing quality is seamless.
    double pos = 0.0;
    if (mpv_get_property(mpv_, "time-pos", MPV_FORMAT_DOUBLE, &pos) < 0 || pos < 0) {
        pos = 0.0;
    }
    pendingSeek_ = pos;
    applySubtitleSettings();
    command({QStringLiteral("loadfile"), currentUrl_});
    setPaused(false);
}

void MpvWidget::stop() {
    inhibitor_.setActive(false);
    command({QStringLiteral("stop")});
}

void MpvWidget::togglePause() {
    command({QStringLiteral("cycle"), QStringLiteral("pause")});
}

void MpvWidget::setPaused(bool paused) {
    setOption(QStringLiteral("pause"), paused ? QStringLiteral("yes") : QStringLiteral("no"));
}

void MpvWidget::seekRelative(double seconds) {
    command({QStringLiteral("seek"), QString::number(seconds), QStringLiteral("relative")});
}

void MpvWidget::seekAbsolute(double seconds) {
    command({QStringLiteral("seek"), QString::number(seconds), QStringLiteral("absolute")});
}

void MpvWidget::setVolume(int percent) {
    setOption(QStringLiteral("volume"), QString::number(percent));
}

void MpvWidget::setMuted(bool muted) {
    setOption(QStringLiteral("mute"), muted ? QStringLiteral("yes") : QStringLiteral("no"));
}

void MpvWidget::setSpeed(double speed) {
    setOption(QStringLiteral("speed"), QString::number(speed));
}

void MpvWidget::setLoop(bool on) {
    setOption(QStringLiteral("loop-file"), on ? QStringLiteral("inf") : QStringLiteral("no"));
}

void MpvWidget::frameStep(int direction) {
    if (!mpv_) {
        return;
    }
    if (direction >= 0) {
        command({QStringLiteral("frame-step")});  // advances one frame and pauses
        return;
    }
    // mpv's frame-back-step is unreliable on streamed sources (it often does
    // nothing), so pause and seek back exactly one frame instead.
    setPaused(true);
    double fps = 0.0;
    if (mpv_get_property(mpv_, "container-fps", MPV_FORMAT_DOUBLE, &fps) < 0 || fps <= 0.0) {
        mpv_get_property(mpv_, "estimated-vf-fps", MPV_FORMAT_DOUBLE, &fps);
    }
    if (fps <= 0.0) {
        fps = 30.0;
    }
    command({QStringLiteral("seek"), QString::number(-1.0 / fps, 'f', 6),
             QStringLiteral("exact")});
}

void MpvWidget::cycleSubtitles() {
    command({QStringLiteral("cycle"), QStringLiteral("sub")});
}

QList<MpvTrack> MpvWidget::tracksOfType(const char* type) const {
    QList<MpvTrack> tracks;
    if (!mpv_) {
        return tracks;
    }
    auto getStr = [this](const QString& prop) {
        QString out;
        char* v = nullptr;
        if (mpv_get_property(mpv_, prop.toUtf8().constData(), MPV_FORMAT_STRING, &v) >= 0 && v) {
            out = QString::fromUtf8(v);
            mpv_free(v);
        }
        return out;
    };
    int64_t count = 0;
    mpv_get_property(mpv_, "track-list/count", MPV_FORMAT_INT64, &count);
    for (int i = 0; i < count; ++i) {
        const QString base = QStringLiteral("track-list/%1/").arg(i);
        if (getStr(base + QStringLiteral("type")) != QLatin1String(type)) {
            continue;
        }
        int64_t id = 0;
        mpv_get_property(mpv_, (base + QStringLiteral("id")).toUtf8().constData(),
                         MPV_FORMAT_INT64, &id);
        int selFlag = 0;
        mpv_get_property(mpv_, (base + QStringLiteral("selected")).toUtf8().constData(),
                         MPV_FORMAT_FLAG, &selFlag);
        const QString lang = getStr(base + QStringLiteral("lang"));
        const QString title = getStr(base + QStringLiteral("title"));
        QStringList parts;
        if (!title.isEmpty()) parts << title;
        if (!lang.isEmpty()) parts << lang.toUpper();
        const QString label = parts.isEmpty() ? QObject::tr("Track %1").arg(id)
                                              : parts.join(QStringLiteral(" · "));
        tracks.push_back({static_cast<int>(id), label, selFlag != 0});
    }
    return tracks;
}

QList<MpvTrack> MpvWidget::subtitleTracks() const {
    return tracksOfType("sub");
}

void MpvWidget::setSubtitleTrack(int id) {
    setOption(QStringLiteral("sid"), id > 0 ? QString::number(id) : QStringLiteral("no"));
}

void MpvWidget::applySubtitleSettings() {
    if (!mpv_) {
        return;
    }
    const QSettings s(apppaths::configFile(), QSettings::IniFormat);
    auto setProp = [this](const char* name, const QByteArray& value) {
        mpv_set_property_string(mpv_, name, value.constData());
    };

    // Which captions to fetch (language + auto-generated opt-in), plus an
    // android client fallback for videos the default client can't access. The
    // client value contains a comma, so escape it with mpv's %length% form.
    bool wantAuto = s.value(QStringLiteral("subtitles/includeAuto"), false).toBool();
    const QString translateTo = s.value(QStringLiteral("subtitles/translateTo")).toString();
    const bool translateOn =
        !translateTo.isEmpty() && s.value(QStringLiteral("subtitles/translateEnabled"), false).toBool();
    QByteArray langPattern;
    if (translateOn) {
        // YT auto-translation is delivered as an auto-caption in the target
        // language, so request that language and enable auto-subs.
        langPattern = translateTo.toUtf8();
        wantAuto = true;
    } else if (wantAuto) {
        // Auto-captions come with a flood of machine translations, so limit them
        // to the preferred language.
        const QString lang =
            s.value(QStringLiteral("subtitles/language"), QStringLiteral("en")).toString();
        langPattern = (lang == QLatin1String("all")) ? "all" : (lang.toUtf8() + ".*");
    } else {
        // Manual tracks are few; fetch them all so multi-language videos list
        // every caption track in the CC menu (the preferred language just picks
        // the default). Auto-generated captions stay opt-in above.
        langPattern = "all";
    }
    const QByteArray clientVal = "youtube:player_client=default,android";
    QByteArray raw = "write-subs=";
    if (wantAuto) {
        raw += ",write-auto-subs=";
    }
    raw += ",sub-langs=" + langPattern;
    raw += ",extractor-args=%" + QByteArray::number(clientVal.size()) + "%" + clientVal;
    setProp("ytdl-raw-options", raw);

    // Styling.
    setProp("sub-font-size",
            QByteArray::number(s.value(QStringLiteral("subtitles/fontSize"), 55).toInt()));
    setProp("sub-color",
            s.value(QStringLiteral("subtitles/color"), QStringLiteral("#FFFFFF"))
                .toString().toUtf8());
    setProp("sub-border-size",
            QByteArray::number(s.value(QStringLiteral("subtitles/outline"), 3).toInt()));
    setProp("sub-bold", s.value(QStringLiteral("subtitles/bold"), false).toBool() ? "yes" : "no");
    // Background box: a semi-opaque black box behind the text, or none.
    setProp("sub-back-color",
            s.value(QStringLiteral("subtitles/background"), false).toBool() ? "#80000000"
                                                                            : "#00000000");

    // Font family (empty → mpv default).
    const QString font = s.value(QStringLiteral("subtitles/font")).toString();
    if (!font.isEmpty()) {
        setProp("sub-font", font.toUtf8());
    }

    // Drop shadow: offset 0 disables it; colour carries its own alpha.
    setProp("sub-shadow-offset",
            QByteArray::number(s.value(QStringLiteral("subtitles/shadowOffset"), 0).toInt()));
    setProp("sub-shadow-color",
            s.value(QStringLiteral("subtitles/shadowColor"), QStringLiteral("#FF000000"))
                .toString().toUtf8());
}
