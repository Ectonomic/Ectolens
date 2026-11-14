/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#pragma once

#include <QWidget>

/**
 * Ectolens: Settings › Private Information. What Hide Private Info finds on its own, and the
 * person's own list of what it always blurs (kcfg alwaysHide): names, places, project names
 * or anything else the patterns can't know. The list is KDE's KEditListWidget, named
 * kcfg_alwaysHide so the settings dialog loads and saves it; PrivateInfoSettings hands it to
 * PrivateInfo whenever it changes.
 */
class PrivateInfoOptionsPage : public QWidget
{
    Q_OBJECT
public:
    explicit PrivateInfoOptionsPage(QWidget *parent = nullptr);
};
