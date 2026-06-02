// FreeFlume — settings page implementation.
#include "apppaths.h"
#include "settingspage.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QKeyCombination>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QVBoxLayout>

#include "database.h"
#include "shortcuts.h"
#include "sponsorcategories.h"
#include "theme.h"

namespace {
QString firstLine(const QString& program, const QStringList& args) {
    QProcess p;
    p.start(program, args);
    if (!p.waitForFinished(3000)) {
        return QStringLiteral("not found");
    }
    return QString::fromUtf8(p.readAllStandardOutput()).section('\n', 0, 0).trimmed();
}
}  // namespace

SettingsPage::SettingsPage(Database* db, QWidget* parent) : QWidget(parent), db_(db) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget(scroll);
    auto* col = new QVBoxLayout(content);
    col->setContentsMargins(18, 16, 18, 16);
    col->setSpacing(14);

    auto* heading = new QLabel(tr("Settings"), content);
    QFont hf = heading->font();
    hf.setBold(true);
    hf.setPointSize(hf.pointSize() + 4);
    heading->setFont(hf);
    col->addWidget(heading);

    // ---- Appearance ----
    auto* appearance = new QGroupBox(tr("Appearance"), content);
    auto* aForm = new QFormLayout(appearance);
    colorScheme_ = new QComboBox(appearance);
    colorScheme_->addItem(tr("Follow system"), QStringLiteral("system"));
    colorScheme_->addItem(tr("Light"), QStringLiteral("light"));
    colorScheme_->addItem(tr("Dark"), QStringLiteral("dark"));
    style_ = new QComboBox(appearance);
    for (const QString& key : theme::availableStyles()) {
        style_->addItem(key == QLatin1String("native") ? tr("Native (default)") : key, key);
    }
    aForm->addRow(tr("Color scheme:"), colorScheme_);
    aForm->addRow(tr("Widget style:"), style_);
    col->addWidget(appearance);

    // ---- Playback ----
    auto* playback = new QGroupBox(tr("Playback"), content);
    auto* pForm = new QFormLayout(playback);
    quality_ = new QComboBox(playback);
    for (const char* q : {"Best", "Auto", "1080p", "720p", "480p", "360p"}) {
        quality_->addItem(QString::fromUtf8(q));
    }
    volume_ = new QSpinBox(playback);
    volume_->setRange(0, 130);
    volume_->setSuffix(QStringLiteral(" %"));
    hwdec_ = new QCheckBox(tr("Use hardware decoding when available"), playback);
    autoplayNext_ = new QCheckBox(tr("Autoplay next video in a playlist"), playback);
    miniPlayer_ = new QCheckBox(tr("Keep playing in a mini-player when navigating away"),
                                playback);
    resumeMode_ = new QComboBox(playback);
    resumeMode_->addItem(tr("Resume where you left off"), QStringLiteral("resume"));
    resumeMode_->addItem(tr("Always ask"), QStringLiteral("ask"));
    resumeMode_->addItem(tr("Always start from beginning"), QStringLiteral("start"));
    pForm->addRow(tr("Preferred video quality:"), quality_);
    pForm->addRow(tr("Default volume:"), volume_);
    pForm->addRow(tr("When reopening a video:"), resumeMode_);
    pForm->addRow(QString(), hwdec_);
    pForm->addRow(QString(), autoplayNext_);
    pForm->addRow(QString(), miniPlayer_);
    col->addWidget(playback);

    // ---- Privacy ----
    auto* hist = new QGroupBox(tr("Privacy"), content);
    auto* hForm = new QFormLayout(hist);
    rememberWatch_ = new QCheckBox(tr("Remember watch history && resume positions"), hist);
    rememberSearch_ = new QCheckBox(tr("Remember search history"), hist);
    clearHistoryBtn_ = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-clear-history")),
                                       tr("Clear Watch History"), hist);
    clearSearchBtn_ = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-clear")),
                                      tr("Clear Search History"), hist);
    hForm->addRow(QString(), rememberWatch_);
    hForm->addRow(QString(), rememberSearch_);
    hForm->addRow(QString(), clearHistoryBtn_);
    hForm->addRow(QString(), clearSearchBtn_);
    col->addWidget(hist);

    // ---- SponsorBlock ----
    auto* sb = new QGroupBox(tr("SponsorBlock"), content);
    auto* sbForm = new QFormLayout(sb);
    sponsorEnabled_ = new QCheckBox(tr("Skip community-marked segments"), sb);
    sbForm->addRow(QString(), sponsorEnabled_);
    for (const sponsor::CategoryInfo& c : sponsor::categories()) {
        auto* combo = new QComboBox(sb);
        combo->addItem(tr("Disabled"), int(sponsor::Mode::Disabled));
        combo->addItem(tr("Auto-skip"), int(sponsor::Mode::Auto));
        combo->addItem(tr("Skip with Enter"), int(sponsor::Mode::Manual));
        sponsorCats_ << combo;
        sbForm->addRow(QCoreApplication::translate("SponsorBlock", c.label) + QStringLiteral(":"),
                       combo);
        // Category controls only matter when SponsorBlock is on.
        connect(sponsorEnabled_, &QCheckBox::toggled, combo, &QWidget::setEnabled);
    }
    col->addWidget(sb);

    // ---- Downloads ----
    auto* downloads = new QGroupBox(tr("Downloads"), content);
    auto* dForm = new QFormLayout(downloads);
    downloadFolderButton_ = new QPushButton(downloads);
    downloadFolderButton_->setIcon(QIcon::fromTheme(QStringLiteral("folder")));
    downloadQuality_ = new QComboBox(downloads);
    downloadQuality_->addItem(tr("Best available"), 0);
    downloadQuality_->addItem(tr("2160p (4K)"), 2160);
    downloadQuality_->addItem(tr("1440p (2K)"), 1440);
    downloadQuality_->addItem(tr("1080p"), 1080);
    downloadQuality_->addItem(tr("720p"), 720);
    downloadQuality_->addItem(tr("480p"), 480);
    downloadQuality_->addItem(tr("360p"), 360);
    embedSubs_ = new QCheckBox(tr("Embed subtitles in video downloads"), downloads);
    embedSubs_->setToolTip(tr("Bakes the preferred-language captions into the video file."));
    dForm->addRow(tr("Save to folder:"), downloadFolderButton_);
    dForm->addRow(tr("Maximum video quality:"), downloadQuality_);
    dForm->addRow(QString(), embedSubs_);
    col->addWidget(downloads);

    // ---- Screenshots ----
    auto* shots = new QGroupBox(tr("Screenshots"), content);
    auto* shForm = new QFormLayout(shots);
    screenshotFolderButton_ = new QPushButton(shots);
    screenshotFolderButton_->setIcon(QIcon::fromTheme(QStringLiteral("folder")));
    screenshotFormat_ = new QComboBox(shots);
    // Highest quality at the top, lowest at the bottom.
    screenshotFormat_->addItem(tr("PNG (Lossless)"), QStringLiteral("png"));
    screenshotFormat_->addItem(tr("JPEG XL (Lossless)"), QStringLiteral("jxl-lossless"));
    screenshotFormat_->addItem(tr("JPEG XL (Lossy)"), QStringLiteral("jxl-lossy"));
    screenshotFormat_->addItem(tr("JPEG"), QStringLiteral("jpg"));
    shForm->addRow(tr("Save to folder:"), screenshotFolderButton_);
    shForm->addRow(tr("Image format:"), screenshotFormat_);
    col->addWidget(shots);

    // ---- Keyboard shortcuts ----
    auto* keys = new QGroupBox(tr("Player keyboard shortcuts"), content);
    auto* kForm = new QFormLayout(keys);
    for (const shortcuts::Action& a : shortcuts::actions()) {
        auto* edit = new QKeySequenceEdit(keys);
        edit->setMaximumSequenceLength(1);  // single-key shortcuts
        edit->setClearButtonEnabled(true);
        connect(edit, &QKeySequenceEdit::editingFinished, this, [this] { save(); });
        shortcutEdits_.insert(a.id, edit);
        kForm->addRow(a.label, edit);
    }
    col->addWidget(keys);

    // ---- Search ----
    auto* search = new QGroupBox(tr("Search"), content);
    auto* sForm = new QFormLayout(search);
    searchLimit_ = new QSpinBox(search);
    searchLimit_->setRange(5, 100);
    includeChannels_ = new QCheckBox(tr("Include Channels"), search);
    includePlaylists_ = new QCheckBox(tr("Include Playlists"), search);
    sForm->addRow(tr("Results per page:"), searchLimit_);
    sForm->addRow(tr("Show in results:"), includeChannels_);
    sForm->addRow(QString(), includePlaylists_);
    col->addWidget(search);

    // ---- Subtitles ----
    auto* subs = new QGroupBox(tr("Subtitles"), content);
    auto* subForm = new QFormLayout(subs);
    includeAutoSubs_ = new QCheckBox(tr("Include auto-generated captions"), subs);
    includeAutoSubs_->setToolTip(
        tr("Auto-captions are available on more videos, but can render as rolling/duplicated lines."));
    subLanguage_ = new QComboBox(subs);
    subLanguage_->addItem(tr("All available"), QStringLiteral("all"));
    subLanguage_->addItem(tr("English"), QStringLiteral("en"));
    subLanguage_->addItem(tr("Spanish"), QStringLiteral("es"));
    subLanguage_->addItem(tr("French"), QStringLiteral("fr"));
    subLanguage_->addItem(tr("German"), QStringLiteral("de"));
    subLanguage_->addItem(tr("Italian"), QStringLiteral("it"));
    subLanguage_->addItem(tr("Portuguese"), QStringLiteral("pt"));
    subLanguage_->addItem(tr("Russian"), QStringLiteral("ru"));
    subLanguage_->addItem(tr("Japanese"), QStringLiteral("ja"));
    subLanguage_->addItem(tr("Korean"), QStringLiteral("ko"));
    subLanguage_->addItem(tr("Chinese"), QStringLiteral("zh"));
    subLanguage_->addItem(tr("Arabic"), QStringLiteral("ar"));
    subLanguage_->addItem(tr("Hindi"), QStringLiteral("hi"));
    subTranslate_ = new QComboBox(subs);
    subTranslate_->setToolTip(
        tr("Have YT auto-translate the captions to this language (uses "
           "auto-captions, which can occasionally render as rolling lines)."));
    subTranslate_->addItem(tr("Off (no translation)"), QString());
    subTranslate_->addItem(tr("English"), QStringLiteral("en"));
    subTranslate_->addItem(tr("Spanish"), QStringLiteral("es"));
    subTranslate_->addItem(tr("French"), QStringLiteral("fr"));
    subTranslate_->addItem(tr("German"), QStringLiteral("de"));
    subTranslate_->addItem(tr("Italian"), QStringLiteral("it"));
    subTranslate_->addItem(tr("Portuguese"), QStringLiteral("pt"));
    subTranslate_->addItem(tr("Russian"), QStringLiteral("ru"));
    subTranslate_->addItem(tr("Japanese"), QStringLiteral("ja"));
    subTranslate_->addItem(tr("Korean"), QStringLiteral("ko"));
    subTranslate_->addItem(tr("Chinese"), QStringLiteral("zh"));
    subTranslate_->addItem(tr("Arabic"), QStringLiteral("ar"));
    subTranslate_->addItem(tr("Hindi"), QStringLiteral("hi"));
    subFont_ = new QFontComboBox(subs);
    subFontSize_ = new QSpinBox(subs);
    subFontSize_->setRange(20, 120);
    subOutline_ = new QSpinBox(subs);
    subOutline_->setRange(0, 10);
    subColorButton_ = new QPushButton(subs);
    subColorButton_->setFixedWidth(96);
    subBackground_ = new QCheckBox(tr("Background box behind text"), subs);
    subBold_ = new QCheckBox(tr("Bold"), subs);
    subShadowOffset_ = new QSpinBox(subs);
    subShadowOffset_->setRange(0, 10);
    subShadowOffset_->setToolTip(tr("0 disables the drop shadow."));
    subShadowColorButton_ = new QPushButton(subs);
    subShadowColorButton_->setFixedWidth(96);
    subForm->addRow(QString(), includeAutoSubs_);
    subForm->addRow(tr("Caption language:"), subLanguage_);
    subForm->addRow(tr("Auto-translate to:"), subTranslate_);
    subForm->addRow(tr("Font:"), subFont_);
    subForm->addRow(tr("Font size:"), subFontSize_);
    subForm->addRow(tr("Text color:"), subColorButton_);
    subForm->addRow(tr("Outline thickness:"), subOutline_);
    subForm->addRow(tr("Shadow offset:"), subShadowOffset_);
    subForm->addRow(tr("Shadow color + alpha:"), subShadowColorButton_);
    subForm->addRow(QString(), subBackground_);
    subForm->addRow(QString(), subBold_);
    col->addWidget(subs);

    // ---- Backends (read-only info) ----
    auto* backends = new QGroupBox(tr("Backends"), content);
    auto* bForm = new QFormLayout(backends);
    bForm->addRow(tr("yt-dlp:"),
                  new QLabel(firstLine(QStringLiteral("yt-dlp"),
                                       {QStringLiteral("--version")}), backends));
    bForm->addRow(tr("mpv (libmpv):"),
                  new QLabel(firstLine(QStringLiteral("mpv"),
                                       {QStringLiteral("--version")}), backends));
    bForm->addRow(tr("Data folder:"),
                  new QLabel(apppaths::dataDir(), backends));
    col->addWidget(backends);

    col->addStretch();
    scroll->setWidget(content);
    outer->addWidget(scroll);

    load();

    // Apply + persist on change.
    connect(colorScheme_, &QComboBox::currentIndexChanged, this, [this] {
        theme::applyColorScheme(colorScheme_->currentData().toString());
        save();
    });
    connect(style_, &QComboBox::currentIndexChanged, this, [this] {
        theme::applyStyle(style_->currentData().toString());
        save();
    });
    connect(quality_, &QComboBox::currentIndexChanged, this, [this] { save(); });
    connect(volume_, &QSpinBox::valueChanged, this, [this] { save(); });
    connect(hwdec_, &QCheckBox::toggled, this, [this] { save(); });
    connect(autoplayNext_, &QCheckBox::toggled, this, [this] { save(); });
    connect(miniPlayer_, &QCheckBox::toggled, this, [this] { save(); });
    connect(resumeMode_, &QComboBox::currentIndexChanged, this, [this] { save(); });
    connect(rememberWatch_, &QCheckBox::toggled, this, [this](bool on) {
        save();
        if (!on && db_) {
            db_->clearHistory();  // turning it off forgets what's stored
        }
    });
    connect(rememberSearch_, &QCheckBox::toggled, this, [this](bool on) {
        save();
        if (!on && db_) {
            db_->clearSearchHistory();
        }
    });
    connect(clearHistoryBtn_, &QPushButton::clicked, this, [this] {
        if (db_) {
            db_->clearHistory();
        }
    });
    connect(clearSearchBtn_, &QPushButton::clicked, this, [this] {
        if (db_) {
            db_->clearSearchHistory();
        }
    });
    connect(sponsorEnabled_, &QCheckBox::toggled, this, [this] { save(); });
    for (QComboBox* combo : sponsorCats_) {
        connect(combo, &QComboBox::currentIndexChanged, this, [this] { save(); });
    }
    connect(embedSubs_, &QCheckBox::toggled, this, [this] { save(); });
    connect(downloadQuality_, &QComboBox::currentIndexChanged, this, [this] { save(); });
    connect(screenshotFormat_, &QComboBox::currentIndexChanged, this, [this] { save(); });
    connect(downloadFolderButton_, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("Choose download folder"), downloadFolder_);
        if (!dir.isEmpty()) {
            downloadFolder_ = dir;
            downloadFolderButton_->setText(dir);
            save();
        }
    });
    connect(screenshotFolderButton_, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("Choose screenshot folder"), screenshotFolder_);
        if (!dir.isEmpty()) {
            screenshotFolder_ = dir;
            screenshotFolderButton_->setText(dir);
            save();
        }
    });
    connect(searchLimit_, &QSpinBox::valueChanged, this, [this] { save(); });
    connect(includeChannels_, &QCheckBox::toggled, this, [this] { save(); });
    connect(includePlaylists_, &QCheckBox::toggled, this, [this] { save(); });
    connect(includeAutoSubs_, &QCheckBox::toggled, this, [this] { save(); });
    connect(subLanguage_, &QComboBox::currentIndexChanged, this, [this] { save(); });
    connect(subTranslate_, &QComboBox::currentIndexChanged, this, [this] { save(); });
    connect(subFontSize_, &QSpinBox::valueChanged, this, [this] { save(); });
    connect(subOutline_, &QSpinBox::valueChanged, this, [this] { save(); });
    connect(subBackground_, &QCheckBox::toggled, this, [this] { save(); });
    connect(subBold_, &QCheckBox::toggled, this, [this] { save(); });
    connect(subColorButton_, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(QColor(subColor_), this, tr("Subtitle Color"));
        if (c.isValid()) {
            subColor_ = c.name(QColor::HexRgb).toUpper();
            subColorButton_->setStyleSheet(
                QStringLiteral("background:%1; color:%2;")
                    .arg(subColor_, c.lightnessF() < 0.5 ? QStringLiteral("#fff")
                                                         : QStringLiteral("#000")));
            subColorButton_->setText(subColor_);
            save();
        }
    });
    connect(subFont_, &QFontComboBox::currentFontChanged, this, [this] { save(); });
    connect(subShadowOffset_, &QSpinBox::valueChanged, this, [this] { save(); });
    connect(subShadowColorButton_, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(QColor(subShadowColor_), this, tr("Shadow Color"),
                                                QColorDialog::ShowAlphaChannel);
        if (c.isValid()) {
            subShadowColor_ = c.name(QColor::HexArgb).toUpper();  // #AARRGGBB
            subShadowColorButton_->setStyleSheet(
                QStringLiteral("background:%1; color:%2;")
                    .arg(c.name(QColor::HexRgb),
                         c.lightnessF() < 0.5 ? QStringLiteral("#fff") : QStringLiteral("#000")));
            subShadowColorButton_->setText(subShadowColor_);
            save();
        }
    });
}

void SettingsPage::load() {
    QSettings s(apppaths::configFile(), QSettings::IniFormat);
    const QString scheme = s.value(QStringLiteral("appearance/colorScheme"),
                                   QStringLiteral("system")).toString();
    colorScheme_->setCurrentIndex(qMax(0, colorScheme_->findData(scheme)));
    const QString style = s.value(QStringLiteral("appearance/style"),
                                  QStringLiteral("native")).toString();
    style_->setCurrentIndex(qMax(0, style_->findData(style)));
    const QString quality = s.value(QStringLiteral("playback/quality"),
                                    QStringLiteral("Auto")).toString();
    quality_->setCurrentIndex(qMax(0, quality_->findText(quality)));
    volume_->setValue(s.value(QStringLiteral("playback/volume"), 100).toInt());
    hwdec_->setChecked(s.value(QStringLiteral("playback/hwdec"), true).toBool());
    autoplayNext_->setChecked(s.value(QStringLiteral("playback/autoplayNext"), true).toBool());
    miniPlayer_->setChecked(s.value(QStringLiteral("playback/miniPlayer"), true).toBool());
    resumeMode_->setCurrentIndex(qMax(0, resumeMode_->findData(
        s.value(QStringLiteral("playback/resumeMode"), QStringLiteral("resume")).toString())));
    rememberWatch_->setChecked(s.value(QStringLiteral("history/rememberWatch"), true).toBool());
    rememberSearch_->setChecked(s.value(QStringLiteral("history/rememberSearch"), true).toBool());

    const bool sbOn = s.value(QStringLiteral("sponsorblock/enabled"), false).toBool();
    sponsorEnabled_->setChecked(sbOn);
    {
        const QList<sponsor::CategoryInfo> cats = sponsor::categories();
        for (int i = 0; i < sponsorCats_.size() && i < cats.size(); ++i) {
            const int def =
                cats.at(i).defaultOn ? int(sponsor::Mode::Auto) : int(sponsor::Mode::Disabled);
            const QString key =
                QStringLiteral("sponsorblock/mode/") + QLatin1String(cats.at(i).key);
            const int mode = s.value(key, def).toInt();
            sponsorCats_.at(i)->setCurrentIndex(qMax(0, sponsorCats_.at(i)->findData(mode)));
            sponsorCats_.at(i)->setEnabled(sbOn);
        }
    }
    downloadFolder_ = s.value(QStringLiteral("downloads/folder"),
                              QStandardPaths::writableLocation(
                                  QStandardPaths::DownloadLocation) +
                                  QStringLiteral("/FreeFlume")).toString();
    downloadFolderButton_->setText(downloadFolder_);
    downloadQuality_->setCurrentIndex(qMax(0, downloadQuality_->findData(
        s.value(QStringLiteral("downloads/maxHeight"), 0).toInt())));
    embedSubs_->setChecked(s.value(QStringLiteral("downloads/embedSubs"), false).toBool());
    screenshotFormat_->setCurrentIndex(qMax(0, screenshotFormat_->findData(
        s.value(QStringLiteral("screenshot/format"), QStringLiteral("png")).toString())));
    screenshotFolder_ = s.value(QStringLiteral("screenshot/folder"),
                                QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
                                    + QStringLiteral("/FreeFlume")).toString();
    screenshotFolderButton_->setText(screenshotFolder_);
    for (auto it = shortcutEdits_.cbegin(); it != shortcutEdits_.cend(); ++it) {
        const int code = shortcuts::keyFor(it.key());
        it.value()->setKeySequence(
            code ? QKeySequence(QKeyCombination::fromCombined(code)) : QKeySequence());
    }

    searchLimit_->setValue(s.value(QStringLiteral("search/limit"), 20).toInt());
    includeChannels_->setChecked(s.value(QStringLiteral("search/includeChannels"), true).toBool());
    includePlaylists_->setChecked(
        s.value(QStringLiteral("search/includePlaylists"), true).toBool());

    includeAutoSubs_->setChecked(s.value(QStringLiteral("subtitles/includeAuto"), false).toBool());
    subLanguage_->setCurrentIndex(qMax(0, subLanguage_->findData(
        s.value(QStringLiteral("subtitles/language"), QStringLiteral("en")).toString())));
    subTranslate_->setCurrentIndex(qMax(0, subTranslate_->findData(
        s.value(QStringLiteral("subtitles/translateTo")).toString())));
    subFontSize_->setValue(s.value(QStringLiteral("subtitles/fontSize"), 55).toInt());
    subOutline_->setValue(s.value(QStringLiteral("subtitles/outline"), 3).toInt());
    subBackground_->setChecked(s.value(QStringLiteral("subtitles/background"), false).toBool());
    subBold_->setChecked(s.value(QStringLiteral("subtitles/bold"), false).toBool());
    subColor_ = s.value(QStringLiteral("subtitles/color"), QStringLiteral("#FFFFFF")).toString();
    const QColor c(subColor_);
    subColorButton_->setStyleSheet(
        QStringLiteral("background:%1; color:%2;")
            .arg(subColor_, c.lightnessF() < 0.5 ? QStringLiteral("#fff") : QStringLiteral("#000")));
    subColorButton_->setText(subColor_);

    const QString font = s.value(QStringLiteral("subtitles/font")).toString();
    if (!font.isEmpty()) {
        const QSignalBlocker block(subFont_);
        subFont_->setCurrentFont(QFont(font));
    }
    subShadowOffset_->setValue(s.value(QStringLiteral("subtitles/shadowOffset"), 0).toInt());
    subShadowColor_ =
        s.value(QStringLiteral("subtitles/shadowColor"), QStringLiteral("#FF000000")).toString();
    const QColor sc(subShadowColor_);
    subShadowColorButton_->setStyleSheet(
        QStringLiteral("background:%1; color:%2;")
            .arg(sc.name(QColor::HexRgb),
                 sc.lightnessF() < 0.5 ? QStringLiteral("#fff") : QStringLiteral("#000")));
    subShadowColorButton_->setText(subShadowColor_);
}

void SettingsPage::save() {
    QSettings s(apppaths::configFile(), QSettings::IniFormat);
    s.setValue(QStringLiteral("appearance/colorScheme"), colorScheme_->currentData());
    s.setValue(QStringLiteral("appearance/style"), style_->currentData());
    s.setValue(QStringLiteral("playback/quality"), quality_->currentText());
    s.setValue(QStringLiteral("playback/volume"), volume_->value());
    s.setValue(QStringLiteral("playback/hwdec"), hwdec_->isChecked());
    s.setValue(QStringLiteral("playback/autoplayNext"), autoplayNext_->isChecked());
    s.setValue(QStringLiteral("playback/miniPlayer"), miniPlayer_->isChecked());
    s.setValue(QStringLiteral("playback/resumeMode"), resumeMode_->currentData());
    s.setValue(QStringLiteral("history/rememberWatch"), rememberWatch_->isChecked());
    s.setValue(QStringLiteral("history/rememberSearch"), rememberSearch_->isChecked());

    s.setValue(QStringLiteral("sponsorblock/enabled"), sponsorEnabled_->isChecked());
    {
        const QList<sponsor::CategoryInfo> cats = sponsor::categories();
        for (int i = 0; i < sponsorCats_.size() && i < cats.size(); ++i) {
            const QString key =
                QStringLiteral("sponsorblock/mode/") + QLatin1String(cats.at(i).key);
            s.setValue(key, sponsorCats_.at(i)->currentData().toInt());
        }
    }
    s.setValue(QStringLiteral("downloads/folder"), downloadFolder_);
    s.setValue(QStringLiteral("downloads/maxHeight"), downloadQuality_->currentData());
    s.setValue(QStringLiteral("downloads/embedSubs"), embedSubs_->isChecked());
    s.setValue(QStringLiteral("screenshot/format"), screenshotFormat_->currentData());
    s.setValue(QStringLiteral("screenshot/folder"), screenshotFolder_);
    for (auto it = shortcutEdits_.cbegin(); it != shortcutEdits_.cend(); ++it) {
        const QKeySequence seq = it.value()->keySequence();
        s.setValue(QStringLiteral("shortcuts/") + it.key(),
                   seq.isEmpty() ? 0 : seq[0].toCombined());
    }
    s.setValue(QStringLiteral("search/limit"), searchLimit_->value());
    s.setValue(QStringLiteral("search/includeChannels"), includeChannels_->isChecked());
    s.setValue(QStringLiteral("search/includePlaylists"), includePlaylists_->isChecked());
    s.setValue(QStringLiteral("subtitles/includeAuto"), includeAutoSubs_->isChecked());
    s.setValue(QStringLiteral("subtitles/language"), subLanguage_->currentData());
    s.setValue(QStringLiteral("subtitles/translateTo"), subTranslate_->currentData());
    s.setValue(QStringLiteral("subtitles/fontSize"), subFontSize_->value());
    s.setValue(QStringLiteral("subtitles/outline"), subOutline_->value());
    s.setValue(QStringLiteral("subtitles/background"), subBackground_->isChecked());
    s.setValue(QStringLiteral("subtitles/bold"), subBold_->isChecked());
    s.setValue(QStringLiteral("subtitles/color"), subColor_);
    s.setValue(QStringLiteral("subtitles/font"), subFont_->currentFont().family());
    s.setValue(QStringLiteral("subtitles/shadowOffset"), subShadowOffset_->value());
    s.setValue(QStringLiteral("subtitles/shadowColor"), subShadowColor_);
}
