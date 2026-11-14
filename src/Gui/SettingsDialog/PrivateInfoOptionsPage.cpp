/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#include "PrivateInfoOptionsPage.h"

#include <KEditListWidget>
#include <KLocalizedString>

#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

static QLabel *note(const QString &text, QWidget *parent)
{
    auto label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QPalette p = label->palette();
    p.setColor(QPalette::WindowText, p.color(QPalette::PlaceholderText));
    label->setPalette(p);
    return label;
}

PrivateInfoOptionsPage::PrivateInfoOptionsPage(QWidget *parent)
    : QWidget(parent)
{
    auto form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    form->addRow(i18nc("@label", "Found for you:"),
                 note(i18nc("@info",
                            "Hide Private Info blurs email addresses, phone numbers, street addresses, keys, tokens, passwords, "
                            "home-folder paths and your name on its own."),
                      this));

    form->addRow(i18nc("@label", "Always hide:"),
                 note(i18nc("@info",
                            "Add what Ectolens can't know: the people you work with, clients, places, a project's code name. Each one "
                            "is blurred wherever it appears as a whole word or words, in capitals or not."),
                      this));
    auto list = new KEditListWidget(this);
    list->setObjectName(u"kcfg_alwaysHide"_s);
    list->setButtons(KEditListWidget::Add | KEditListWidget::Remove);
    list->lineEdit()->setPlaceholderText(i18nc("@info:placeholder", "A name, a place, a project…"));
    form->addRow(QString(), list);

    auto outer = new QVBoxLayout(this);
    outer->addLayout(form);
    outer->addStretch();
}
