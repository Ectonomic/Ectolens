/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#pragma once

#include <QWidget>

class KMessageWidget;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QSpinBox;
class VideoSaveOptionsPage;

/**
 * Ectolens: Settings › Recording. It opens with what recordings are right now,
 * then Quality, Picture, Encoder, Sound and File, in the order a recording
 * works. Simple settings (frame rate, sound, file) are kcfg_* widgets the dialog
 * keeps; the quality choice and the encoder's values are kept by this page,
 * because under Small, High or Best the encoder fields show that choice's own
 * numbers rather than the saved custom ones.
 */
class RecordingOptionsPage : public QWidget
{
    Q_OBJECT

public:
    explicit RecordingOptionsPage(QWidget *parent = nullptr);

    bool hasChanges() const;
    bool isDefault() const;
    void save();
    void load();
    void applyDefaults();

Q_SIGNALS:
    void changed();

private:
    struct Values {
        int quality = 1;
        QString encoder;
        QString speed;
        double level = 16;
        double keyframes = 2;
        int bFrames = -1;
        bool fullColor = false;
        QString extra;
        bool operator==(const Values &) const = default;
    };
    Values current() const;
    Values saved() const;
    void display(const Values &values);
    void choosePreset(int preset);
    void markCustom();
    void fillSpeedPresets(const QString &encoderName);
    void updateStatus();
    void checkSpeed();

    KMessageWidget *m_status = nullptr;
    QButtonGroup *m_quality = nullptr;
    QComboBox *m_encoder = nullptr;
    QSpinBox *m_framerate = nullptr;
    QComboBox *m_speed = nullptr;
    QDoubleSpinBox *m_level = nullptr;
    QDoubleSpinBox *m_keyframes = nullptr;
    QSpinBox *m_bFrames = nullptr;
    QCheckBox *m_fullColor = nullptr;
    QLineEdit *m_extra = nullptr;
    VideoSaveOptionsPage *m_filePage = nullptr;
    QString m_speedResult;
    bool m_syncing = false;
};
