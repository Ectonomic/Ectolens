/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#include "AgentsOptionsPage.h"

#include <KLocalizedString>

#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QFormLayout>
#include <QLabel>
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

AgentsOptionsPage::AgentsOptionsPage(QWidget *parent)
    : QWidget(parent)
{
    auto form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto access = new QCheckBox(i18nc("@option:check", "Let AI agents ask for pictures and recordings"), this);
    access->setObjectName(u"kcfg_agentAccess"_s);
    form->addRow(i18nc("@label", "Agents:"), access);
    form->addRow(QString(),
                 note(i18nc("@info",
                            "An agent, such as Claude Code, asks through Ectolens's MCP server. Each request opens the overlay framed on "
                            "what it asked for, and nothing is sent until you press Send."),
                      this));

    auto hide = new QCheckBox(i18nc("@option:check", "Blur private information before you send"), this);
    hide->setObjectName(u"kcfg_agentHidePrivateInfo"_s);
    form->addRow(i18nc("@label", "Privacy:"), hide);
    form->addRow(QString(),
                 note(i18nc("@info", "Email addresses, keys, tokens and home-folder paths, found as Hide Private Info finds them. Window titles an agent lists are blurred the same way."),
                      this));

    const QString server = QDir(QCoreApplication::applicationDirPath()).filePath(u"ectolens-mcp"_s);
    auto connectLabel = note(i18nc("@info %1 a command", "To connect Claude Code, run: %1", u"claude mcp add ectolens -- "_s + server), this);
    form->addRow(i18nc("@label", "Connect:"), connectLabel);

    auto outer = new QVBoxLayout(this);
    outer->addLayout(form);
    outer->addStretch();
}
