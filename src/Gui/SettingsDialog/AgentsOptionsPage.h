/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#pragma once

#include <QWidget>

/**
 * Ectolens: Settings › Model Context Protocol (MCP). Whether AI agents may ask for captures and recordings through
 * Ectolens's MCP server (ectolens-mcp), whether private information is blurred before the
 * person sends one, and how to connect an agent. Off until the person turns it on, and every
 * request waits for them to press Send in the overlay.
 *
 * Its check boxes are named kcfg_<entry> so the settings dialog loads and saves them.
 */
class AgentsOptionsPage : public QWidget
{
    Q_OBJECT
public:
    explicit AgentsOptionsPage(QWidget *parent = nullptr);
};
