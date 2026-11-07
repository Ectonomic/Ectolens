/*
 *  SPDX-FileCopyrightText: 2025 Jhair Paris <dev@jhairparis.com>
 *  SPDX-FileCopyrightText: 2019 David Redondo <kde@david-redondo.de>
 *  SPDX-FileCopyrightText: 2015 Boudhayan Gupta <bgupta@kde.org>
 *
 *  SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "GeneralOptionsPage.h"

#include "OcrLanguageSelector.h"
#include "OcrManager.h"
#include "ui_GeneralOptions.h"

#include <KLocalizedString>
#include <KWindowSystem>

#include "Gui/CaptureTools.h"
#include "settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QIcon>
#include <QLineEdit>
#include <QStandardItemModel>

using namespace Qt::Literals::StringLiterals;

GeneralOptionsPage::GeneralOptionsPage(QWidget *parent)
    : QWidget(parent)
    , m_ui(new Ui_GeneralOptions)
    , m_ocrLanguageSelector(new OcrLanguageSelector(this))
{
    m_ui->setupUi(this);

    m_ui->ocrInfoIcon->setPixmap(QIcon::fromTheme(QStringLiteral("help-hint")).pixmap(16, 16));
    m_ui->ocrInfoIcon->setCursor(Qt::WhatsThisCursor);

    m_ui->closeAfterOcrInfo->setPixmap(QIcon::fromTheme(QStringLiteral("help-hint")).pixmap(16, 16));
    m_ui->closeAfterOcrInfo->setCursor(Qt::WhatsThisCursor);

    buildAfterCapture();

    m_ui->runningTitle->setLevel(2);
    m_ui->regionTitle->setLevel(2);
    m_ui->ocrTitle->setLevel(2);

    m_ui->ocrLanguageScrollArea->setWidget(m_ocrLanguageSelector);
    m_ui->ocrLanguageScrollArea->setWidgetResizable(true);

    connect(m_ocrLanguageSelector, &OcrLanguageSelector::selectedLanguagesChanged, this, &GeneralOptionsPage::ocrLanguageChanged);

    if (OcrManager *ocrManager = OcrManager::instance()) {
        auto updateProcessingUiState = [this](OcrManager::OcrStatus status) {
            const bool busy = status == OcrManager::OcrStatus::Processing;
            m_ui->ocrLanguageLabel->setEnabled(!busy);
            m_ui->ocrLanguageScrollArea->setEnabled(!busy);
        };

        connect(ocrManager, &OcrManager::statusChanged, this, updateProcessingUiState);
        updateProcessingUiState(ocrManager->status());
    }

    refreshOcrLanguageSettings(false);

    //On Wayland  we can't programmatically raise and focus the window so we have to hide the option
    if (KWindowSystem::isPlatformWayland() || qstrcmp(qgetenv("XDG_SESSION_TYPE").constData(), "wayland") == 0) {
        m_ui->kcfg_printKeyRunningAction->removeItem(2);
    }
}

GeneralOptionsPage::~GeneralOptionsPage() = default;

// Ectolens: the same After Capture list as Capture's arrow in the overlay (design proposal 2):
// a check per feature, so they stack, and a choice at the end of the row says how.
// The widgets are named kcfg_<entry> so the settings dialog loads and saves them.
void GeneralOptionsPage::buildAfterCapture()
{
    auto grid = new QGridLayout(m_ui->afterCaptureBox);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setColumnStretch(2, 1);
    auto check = [this](const QString &entry, const QString &text) {
        auto box = new QCheckBox(text, m_ui->afterCaptureBox);
        box->setObjectName(u"kcfg_"_s + entry);
        return box;
    };
    auto combo = [this](const QString &entry, const QStringList &items) {
        auto c = new QComboBox(m_ui->afterCaptureBox);
        if (!entry.isEmpty()) {
            c->setObjectName(u"kcfg_"_s + entry);
        }
        c->addItems(items);
        return c;
    };
    // A string setting chosen from a list: a hidden line edit holds the value for the dialog,
    // and the combo shows its name.
    auto stringChoice = [this](const QString &entry, const QVariantList &choices, QComboBox *c) {
        auto value = new QLineEdit(m_ui->afterCaptureBox);
        value->setObjectName(u"kcfg_"_s + entry);
        value->hide();
        for (const auto &v : choices) {
            const auto m = v.toMap();
            c->addItem(QIcon::fromTheme(m.value(u"icon"_s).toString()), m.value(u"name"_s).toString(), m.value(u"id"_s));
        }
        connect(c, &QComboBox::activated, value, [c, value](int i) {
            value->setText(c->itemData(i).toString());
        });
        connect(value, &QLineEdit::textChanged, c, [c](const QString &id) {
            int i = c->findData(id);
            if (i < 0 && !id.isEmpty()) {
                // saved, but no longer installed: say so instead of showing another entry
                c->addItem(i18nc("@item %1 an app or service that was removed", "%1 (not installed)", id), id);
                i = c->count() - 1;
            }
            c->setCurrentIndex(std::max(i, 0));
        });
        return value;
    };

    auto tools = CaptureTools::instance();
    auto open = check(u"afterCaptureOpen"_s, i18nc("@option:check after a capture", "Open with"));
    auto openApp = combo({}, {});
    stringChoice(u"afterCaptureOpenApp"_s, tools->openWithApps(), openApp);
    auto save = check(u"autoSaveImage"_s, i18nc("@option:check after a capture", "Save to file"));
    auto where = combo(u"afterCaptureSaveWhere"_s, {i18nc("@item save to", "Screenshots"), i18nc("@item save to", "Ask where each time")});
    auto folder = check(u"afterCaptureShowInFolder"_s, i18nc("@option:check after a capture", "Show in folder"));
    auto copy = check(u"afterCaptureCopy"_s, i18nc("@option:check after a capture", "Copy to clipboard"));
    auto what = combo(u"afterCaptureCopyWhat"_s, {i18nc("@item copy", "Image"), i18nc("@item copy", "File Path")});
    auto share = check(u"afterCaptureShare"_s, i18nc("@option:check after a capture", "Share"));
    auto shareTo = combo({}, {});
    QVariantList targets{QVariantMap{{u"id"_s, QString()}, {u"name"_s, i18nc("@item share service", "Ask each time")}}};
    targets << tools->shareTargets();
    stringChoice(u"afterCaptureShareTarget"_s, targets, shareTo);

    int row = 0;
    for (auto [box, choice] : std::initializer_list<std::pair<QCheckBox *, QComboBox *>>{{open, openApp}, {save, where}, {folder, nullptr}, {copy, what}, {share, shareTo}}) {
        grid->addWidget(box, row, 0);
        if (choice) {
            grid->addWidget(choice, row, 1);
        }
        ++row;
    }

    // File Path and Show in folder need the saved file, so they wait for Save to file.
    auto *pathItem = qobject_cast<QStandardItemModel *>(what->model());
    auto lock = [=] {
        const bool saving = save->isChecked();
        folder->setEnabled(saving);
        if (pathItem) {
            pathItem->item(1)->setEnabled(saving);
        }
        if (!saving && what->currentIndex() == 1) {
            what->setCurrentIndex(0);
        }
        openApp->setEnabled(open->isChecked());
        where->setEnabled(save->isChecked());
        what->setEnabled(copy->isChecked());
        shareTo->setEnabled(share->isChecked());
    };
    for (auto box : {open, save, copy, share}) {
        connect(box, &QCheckBox::toggled, this, lock);
    }
    lock();
}

void GeneralOptionsPage::refreshOcrLanguageSettings(bool rebuildSelector)
{
    OcrManager *ocrManager = OcrManager::instance();
    const bool ocrAvailable = ocrManager->isAvailable();

    if (!ocrAvailable) {
        m_ui->ocrLanguageLabel->setVisible(false);
        m_ui->ocrLanguageScrollArea->setVisible(false);
        m_ui->closeAfterOcrLabel->setVisible(false);
        m_ui->kcfg_closeAfterOcr->setVisible(false);
        m_ui->closeAfterOcrInfo->setVisible(false);
        m_ui->ocrUnavailableWidget->setVisible(true);
    } else {
        m_ui->ocrLanguageLabel->setVisible(true);
        m_ui->ocrLanguageScrollArea->setVisible(true);
        m_ui->closeAfterOcrLabel->setVisible(true);
        m_ui->kcfg_closeAfterOcr->setVisible(true);
        m_ui->closeAfterOcrInfo->setVisible(true);
        m_ui->ocrUnavailableWidget->setVisible(false);
    }

    if (ocrAvailable && rebuildSelector) {
        m_ocrLanguageSelector->refresh();
    }
}

#include "moc_GeneralOptionsPage.cpp"
