/*
 *  SPDX-FileCopyrightText: 2025 Jhair Paris <dev@jhairparis.com>
 *  SPDX-FileCopyrightText: 2019 David Redondo <kde@david-redondo.de>
 *  SPDX-FileCopyrightText: 2015 Boudhayan Gupta <bgupta@kde.org>
 *
 *  SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "SettingsDialog.h"

#include "AppearanceOptionsPage.h"
#include "AgentsOptionsPage.h"
#include "GeneralOptionsPage.h"
#include "ImageSaveOptionsPage.h"
#include "OcrLanguageSelector.h"
#include "OcrManager.h"
#include "ShortcutsOptionsPage.h"
#include "RecordingOptionsPage.h"
#include "Gui/CaptureWindow.h"
#include "settings.h"

#include <QFontDatabase>
#include <QScreen>
#include <QStyle>
#include <QWindow>

#include <KLocalizedString>
#include <KShortcutWidget>

using namespace Qt::StringLiterals;

SettingsDialog::SettingsDialog(QWidget *parent)
    : KConfigDialog(parent, "settings"_L1, Settings::self())
    , m_generalPage(new GeneralOptionsPage(this))
    , m_imagesPage(new ImageSaveOptionsPage(this))
    , m_videosPage(new RecordingOptionsPage(this))
    , m_shortcutsPage(new ShortcutsOptionsPage(this))
    , m_appearancePage(new AppearanceOptionsPage(this))
{
    setFaceType(KPageDialog::List);
    addPage(m_generalPage, Settings::self(), i18nc("Settings category", "General"), "spectacle"_L1);
    addPage(m_imagesPage, Settings::self(), i18nc("Settings category", "Image Saving"), "image-x-generic"_L1);
    m_recordingItem = addPage(m_videosPage, Settings::self(), i18nc("Settings category", "Recording"), "camera-video"_L1);
    m_appearanceItem = addPage(m_appearancePage, Settings::self(), i18nc("Settings category", "Appearance"), "preferences-desktop-color"_L1);
    addPage(new AgentsOptionsPage(this), Settings::self(), i18nc("Settings category", "Model Context Protocol (MCP)"), "preferences-plugin"_L1);
    m_shortcutsItem = addPage(m_shortcutsPage, i18nc("Settings category", "Shortcuts"), "preferences-desktop-keyboard"_L1);
    connect(m_shortcutsPage, &ShortcutsOptionsPage::shortCutsChanged, this, [this] {
        updateButtons();
    });
    connect(m_generalPage, &GeneralOptionsPage::ocrLanguageChanged, this, [this] {
        updateButtons();
    });
    connect(m_videosPage, &RecordingOptionsPage::changed, this, [this] {
        updateButtons();
    });
    connect(this, &KConfigDialog::currentPageChanged, this, &SettingsDialog::updateButtons);
}

void SettingsDialog::showRecordingPage()
{
    setCurrentPage(m_recordingItem);
}

void SettingsDialog::showAppearancePage()
{
    setCurrentPage(m_appearanceItem);
}

void SettingsDialog::showShortcutsPage()
{
    setCurrentPage(m_shortcutsItem);
}

void SettingsDialog::hideEvent(QHideEvent *event)
{
    CaptureWindow::setAllBelowDialogs(false);
    KConfigDialog::hideEvent(event);
}

SettingsDialog::~SettingsDialog()
{
    // Ensure OCR config sync is resumed
    if (OcrManager *ocrManager = OcrManager::instance()) {
        if (ocrManager->isConfigSyncSuspended()) {
            ocrManager->setConfigSyncSuspended(false);
        }
    }
}

QSize SettingsDialog::sizeHint() const
{
    // Avoid having pages that need to be scrolled,
    // unless size is larger than available screen height.
    const auto headerSize = pageWidget()->pageHeader()->sizeHint();
    const auto footerSize = pageWidget()->pageFooter()->sizeHint();
    auto sh = m_generalPage->sizeHint();
    sh = sh.expandedTo(m_imagesPage->sizeHint());
    sh = sh.expandedTo(m_videosPage->sizeHint());
    sh = sh.expandedTo(m_shortcutsPage->sizeHint());
    sh = sh.expandedTo(m_appearancePage->sizeHint());
    sh.rheight() += headerSize.height() + footerSize.height()
                 + style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing) * 2;
    sh = KConfigDialog::sizeHint().expandedTo(sh);
    const auto screenHeight = screen() ? screen()->availableGeometry().height() : 0;
    sh.setHeight(std::min(sh.height(), screenHeight));
    return sh;
}

void SettingsDialog::showEvent(QShowEvent *event)
{
    auto parent = parentWidget();
    bool onTop = parent && parent->windowHandle()->flags().testFlag(Qt::WindowStaysOnTopHint);
    windowHandle()->setFlag(Qt::WindowStaysOnTopHint, onTop);
    
    m_generalPage->refreshOcrLanguageSettings();

    // Ectolens: the capture overlay sits on the overlay layer; let this dialog be seen.
    CaptureWindow::setAllBelowDialogs(true);

    KConfigDialog::showEvent(event);
}

bool SettingsDialog::hasChanged()
{
    bool ocrHasChanges = false;
    if (OcrManager::instance()->isAvailable()) {
        ocrHasChanges = m_generalPage->ocrLanguageSelector()->hasChanges();
    }
    return m_shortcutsPage->isModified() || ocrHasChanges || m_videosPage->hasChanges() || KConfigDialog::hasChanged();
}

bool SettingsDialog::isDefault()
{
    bool ocrIsDefault = true;
    if (OcrManager::instance()->isAvailable()) {
        ocrIsDefault = m_generalPage->ocrLanguageSelector()->isDefault();
    }
    return currentPage()->name() != i18n("Shortcuts") && ocrIsDefault && m_videosPage->isDefault() && KConfigDialog::isDefault();
}

void SettingsDialog::updateSettings()
{
    KConfigDialog::updateSettings();
    m_shortcutsPage->saveChanges();
    m_videosPage->save();

    if (OcrManager::instance()->isAvailable()) {
        m_generalPage->ocrLanguageSelector()->saveSettings();
    }

    if (OcrManager *ocrManager = OcrManager::instance()) {
        ocrManager->setConfigSyncSuspended(false);
    }
}

void SettingsDialog::updateWidgets()
{
    KConfigDialog::updateWidgets();
    m_shortcutsPage->resetChanges();
    m_videosPage->load();

    if (OcrManager::instance()->isAvailable()) {
        m_generalPage->ocrLanguageSelector()->updateWidgets();
    }
    m_generalPage->refreshOcrLanguageSettings();

    if (OcrManager *ocrManager = OcrManager::instance()) {
        ocrManager->setConfigSyncSuspended(false);
    }
}

void SettingsDialog::updateWidgetsDefault()
{
    if (OcrManager *ocrManager = OcrManager::instance()) {
        ocrManager->setConfigSyncSuspended(true);
    }

    KConfigDialog::updateWidgetsDefault();
    m_shortcutsPage->defaults();
    m_videosPage->applyDefaults();

    if (OcrManager::instance()->isAvailable()) {
        m_generalPage->ocrLanguageSelector()->applyDefaults();
    }
    m_generalPage->refreshOcrLanguageSettings(false);
}

#include "moc_SettingsDialog.cpp"
