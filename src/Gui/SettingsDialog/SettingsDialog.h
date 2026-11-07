/*
 *  SPDX-FileCopyrightText: 2015 Boudhayan Gupta <bgupta@kde.org>
 *
 *  SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <KConfigDialog>

class AppearanceOptionsPage;
class GeneralOptionsPage;
class ImageSaveOptionsPage;
class RecordingOptionsPage;
class KPageWidgetItem;
class ShortcutsOptionsPage;

class SettingsDialog : public KConfigDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog() override;

    /// Ectolens: open on Settings › Recording
    void showRecordingPage();
    /// Ectolens: open on Settings › Appearance or Shortcuts
    void showAppearancePage();
    void showShortcutsPage();

protected:
    QSize sizeHint() const override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    bool hasChanged() override;
    bool isDefault() override;
    void updateSettings() override;
    void updateWidgets() override;
    void updateWidgetsDefault() override;

    GeneralOptionsPage *const m_generalPage;
    ImageSaveOptionsPage *const m_imagesPage;
    RecordingOptionsPage *const m_videosPage;
    KPageWidgetItem *m_recordingItem = nullptr;
    ShortcutsOptionsPage *const m_shortcutsPage;
    AppearanceOptionsPage *const m_appearancePage;
    KPageWidgetItem *m_appearanceItem = nullptr;
    KPageWidgetItem *m_shortcutsItem = nullptr;
};

#endif // SETTINGSDIALOG_H
