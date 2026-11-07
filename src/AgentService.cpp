/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#include "AgentService.h"

#include "PrivateInfo.h"
#include "SpectacleCore.h"
#include "Platforms/WindowPicker.h"
#include "settings.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

using namespace Qt::StringLiterals;

static QString json(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

AgentService::AgentService(QObject *parent)
    : QObject(parent)
{
}

/// a window title with what looks private in it blurred into words an agent can't read
static QString clean(const QString &title)
{
    if (!Settings::agentHidePrivateInfo()) {
        return title;
    }
    QStringList words = title.split(u' ');
    for (auto &word : words) {
        if (PrivateInfo::looksPrivate(word)) {
            word = u"[hidden]"_s;
        }
    }
    return words.join(u' ');
}

QString AgentService::Windows()
{
    if (!Settings::agentAccess()) {
        return json({{u"status"_s, u"off"_s}});
    }
    QJsonArray list;
    for (const auto &w : WindowPicker::instance()->visibleWindows()) {
        list.append(QJsonObject{{u"id"_s, w.uuid},
                                {u"app"_s, WindowPicker::appName(w)},
                                {u"title"_s, clean(w.title)},
                                {u"x"_s, w.geometry.x()},
                                {u"y"_s, w.geometry.y()},
                                {u"width"_s, w.geometry.width()},
                                {u"height"_s, w.geometry.height()},
                                {u"active"_s, w.active}});
    }
    return json({{u"status"_s, u"ok"_s}, {u"windows"_s, list}});
}

static SpectacleCore::AgentRequest requestFrom(const QString &text, const QString &kind)
{
    const QJsonObject o = QJsonDocument::fromJson(text.toUtf8()).object();
    SpectacleCore::AgentRequest r;
    r.kind = kind;
    r.target = o.value(u"target"_s).toString(u"screen"_s);
    r.windowId = o.value(u"window"_s).toString();
    const QJsonObject a = o.value(u"area"_s).toObject();
    r.area = QRectF(a.value(u"x"_s).toDouble(), a.value(u"y"_s).toDouble(), a.value(u"width"_s).toDouble(), a.value(u"height"_s).toDouble());
    r.reason = o.value(u"reason"_s).toString().left(300);
    r.client = o.value(u"client"_s).toString().left(60);
    r.wantText = o.value(u"text"_s).toBool();
    return r;
}

// A reply that waits for the person: D-Bus holds the call open until Ectolens answers it.
static SpectacleCore::AgentReply later(const QDBusMessage &message)
{
    return [message](const QJsonObject &result) {
        QDBusConnection::sessionBus().send(message.createReply(json(result)));
    };
}

QString AgentService::Capture(const QString &request)
{
    setDelayedReply(true);
    SpectacleCore::instance()->agentCapture(requestFrom(request, u"capture"_s), later(message()));
    return {};
}

QString AgentService::RecordStart(const QString &request)
{
    setDelayedReply(true);
    SpectacleCore::instance()->agentCapture(requestFrom(request, u"record"_s), later(message()));
    return {};
}

QString AgentService::RecordStop()
{
    setDelayedReply(true);
    SpectacleCore::instance()->agentRecordStop(later(message()));
    return {};
}
