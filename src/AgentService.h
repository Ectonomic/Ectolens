/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#pragma once

#include <QDBusContext>
#include <QObject>

/**
 * Ectolens: what an AI agent may ask of Ectolens, on the session bus at /Agent
 * (com.ectonomic.Ectolens.Agent). ectolens-mcp, the MCP server, is its one caller.
 *
 * Every method answers with a JSON object as a string. Nothing works until the person turns on
 * Settings › Model Context Protocol (MCP) ("status": "off"), and a capture or a recording opens the overlay framed on
 * what the agent asked for and waits for the person: Send gives it to the agent, Don't Send or
 * Esc refuses ("status": "declined"). The reply waits as long as the person does.
 *
 *   Windows()               {"status": "ok", "windows": [{"id", "app", "title", "x", "y", "width", "height", "active"}]}
 *   Capture(request)        {"status": "sent", "path", "width", "height", "text"?} once the person sends it
 *   RecordStart(request)    {"status": "recording"} once the person starts it
 *   RecordStop()            {"status": "saved", "path"} once the file is written
 *
 * A request: {"target": "screen" | "window" | "active_window" | "area", "window": id,
 * "area": {"x", "y", "width", "height"}, "reason": words the person reads, "client": the agent's
 * name, "text": true to read the words in the picture as well}.
 */
class AgentService : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.ectonomic.Ectolens.Agent")
public:
    explicit AgentService(QObject *parent = nullptr);

public Q_SLOTS:
    QString Windows();
    QString Capture(const QString &request);
    QString RecordStart(const QString &request);
    QString RecordStop();
};
