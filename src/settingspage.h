// FreeFlume — settings page (QSettings-backed).
#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QWidget>

class Database;
class QComboBox;
class QSpinBox;
class QCheckBox;
class QLabel;
class QPushButton;
class QFontComboBox;
class QKeySequenceEdit;

class SettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(Database* db, QWidget* parent = nullptr);

private:
    void load();
    void save();

    Database* db_ = nullptr;

    QComboBox* colorScheme_ = nullptr;
    QComboBox* style_ = nullptr;
    QCheckBox* notifyUpdates_ = nullptr;
    QPushButton* checkUpdatesBtn_ = nullptr;
    QComboBox* quality_ = nullptr;
    QSpinBox* volume_ = nullptr;
    QComboBox* hwdecMode_ = nullptr;
    QCheckBox* autoplayNext_ = nullptr;
    QCheckBox* miniPlayer_ = nullptr;
    QComboBox* resumeMode_ = nullptr;
    QCheckBox* rememberWatch_ = nullptr;
    QCheckBox* rememberSearch_ = nullptr;
    QPushButton* clearHistoryBtn_ = nullptr;
    QPushButton* clearSearchBtn_ = nullptr;
    QSpinBox* searchLimit_ = nullptr;
    QCheckBox* includeChannels_ = nullptr;
    QCheckBox* includePlaylists_ = nullptr;
    QCheckBox* searchSuggestions_ = nullptr;
    QCheckBox* includeAutoSubs_ = nullptr;
    QComboBox* subLanguage_ = nullptr;
    QComboBox* subTranslate_ = nullptr;
    QFontComboBox* subFont_ = nullptr;
    QSpinBox* subFontSize_ = nullptr;
    QPushButton* subColorButton_ = nullptr;
    QSpinBox* subOutline_ = nullptr;
    QCheckBox* subBackground_ = nullptr;
    QCheckBox* subBold_ = nullptr;
    QSpinBox* subShadowOffset_ = nullptr;
    QPushButton* subShadowColorButton_ = nullptr;
    QCheckBox* sponsorEnabled_ = nullptr;
    QList<QComboBox*> sponsorCats_;  // per-category mode, aligned with sponsor::categories()
    QPushButton* downloadFolderButton_ = nullptr;
    QComboBox* downloadQuality_ = nullptr;
    QCheckBox* embedSubs_ = nullptr;
    QCheckBox* embedAllAudio_ = nullptr;   // mux every audio language into one mkv
    QCheckBox* embedAutoDubs_ = nullptr;   // include machine dubs (nested under above)
    QComboBox* screenshotFormat_ = nullptr;
    QPushButton* screenshotFolderButton_ = nullptr;
    QString screenshotFolder_;
    QHash<QString, QKeySequenceEdit*> shortcutEdits_;
    QString downloadFolder_;
    QString subColor_ = QStringLiteral("#FFFFFF");
    QString subShadowColor_ = QStringLiteral("#FF000000");
};
