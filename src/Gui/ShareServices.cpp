/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#include "ShareServices.h"

#include "Config.h"

#include <KApplicationTrader>
#include <KLazyLocalizedString>
#include <KLocalizedString>
#include <KService>
#include <KShell>

#ifdef PURPOSE_FOUND
#include <Purpose/AlternativesModel>
#endif

#include <QDBusConnection>
#include <QDBusMessage>
#include <QJsonArray>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QStandardPaths>
#include <optional>

using namespace Qt::StringLiterals;

// The services Ectolens never offers, and why
static const struct {
    QLatin1StringView id;
    KLazyLocalizedString why;
} retired[] = {
    {"clipboardplugin"_L1, kli18nc("@info why a share service is left out", "After Capture has its own Copy to clipboard row")},
    {"saveasplugin"_L1, kli18nc("@info why a share service is left out", "After Capture's Save to file asks where each time, if you choose")},
    // KDE bugs 452470 (no confirmation) and 515955 (uploads fail since February 2026)
    {"imgurplugin"_L1, kli18nc("@info why a share service is left out", "It uploads to a public website without asking, and its uploads fail")},
    // KDE Online Accounts' Google sign-in, which Google blocks for new sign-ins
    {"youtubeplugin"_L1, kli18nc("@info why a share service is left out", "It signs in through KDE's Google account connection, which Google blocks")},
};

bool ShareServices::isRetired(const QString &pluginId)
{
    for (const auto &r : retired) {
        if (pluginId == r.id) {
            return true;
        }
    }
    return false;
}

QString ShareServices::retiredBecause(const QString &pluginId)
{
    for (const auto &r : retired) {
        if (pluginId == r.id) {
            return r.why.toString();
        }
    }
    return {};
}

bool ShareServices::hasMailApp()
{
    // Send via Email hands the file to the app that opens mailto: links (KIO's
    // KEMailClientLauncherJob); with none, it does nothing and says nothing (KDE bug 455847)
    // KDE 4's leftover kmailservice is such an app on some systems: a hidden helper that hands
    // the link on to KDE 4's email setting, and without KMail opens nothing. An app counts
    // when it is one a person can see in the menu and its program is installed.
    const auto app = KApplicationTrader::preferredService(u"x-scheme-handler/mailto"_s);
    if (!app || app->noDisplay()) {
        return false;
    }
    const QString program = KShell::splitArgs(app->exec()).value(0);
    return !program.isEmpty() && !QStandardPaths::findExecutable(program).isEmpty();
}

bool ShareServices::hasPairedDevice()
{
    auto call = QDBusMessage::createMethodCall(u"org.kde.kdeconnect"_s, u"/modules/kdeconnect"_s, u"org.kde.kdeconnect.daemon"_s, u"devices"_s);
    call << false /* only reachable */ << true /* only paired */;
    call.setAutoStartService(false); // asking must not start KDE Connect
    const auto reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 500);
    return reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().value(0).toStringList().isEmpty();
}

QVariantList ShareServices::all(const QUrl &sample)
{
    QVariantList list;
#ifdef PURPOSE_FOUND
    Purpose::AlternativesModel model;
    model.setInputData(QJsonObject{{u"mimeType"_s, QMimeDatabase().mimeTypeForUrl(sample).name()}, {u"urls"_s, QJsonArray{sample.toString()}}});
    model.setPluginType(u"Export"_s);
    std::optional<bool> mail, paired; // asked once, and only when the service is there
    for (int row = 0; row < model.rowCount(); ++row) {
        const auto index = model.index(row);
        const QString id = model.data(index, Purpose::AlternativesModel::PluginIdRole).toString();
        QString why = retiredBecause(id);
        if (why.isEmpty() && id == u"emailplugin"_s) {
            if (!mail) {
                mail = hasMailApp();
            }
            if (!*mail) {
                why = i18nc("@info why a share service is left out", "No email app is set up");
            }
        }
        if (why.isEmpty() && id == u"kdeconnectplugin"_s) {
            if (!paired) {
                paired = hasPairedDevice();
            }
            if (!*paired) {
                why = i18nc("@info why a share service is left out", "No device is paired with KDE Connect");
            }
        }
        list << QVariantMap{{u"id"_s, id},
                            {u"name"_s, model.data(index, Purpose::AlternativesModel::ActionDisplayRole)},
                            {u"icon"_s, model.data(index, Purpose::AlternativesModel::IconNameRole)},
                            {u"why"_s, why}};
    }
#else
    Q_UNUSED(sample)
#endif
    return list;
}

QVariantList ShareServices::offered(const QUrl &sample)
{
    QVariantList list;
    for (const auto &v : all(sample)) {
        auto m = v.toMap();
        if (m.value(u"why"_s).toString().isEmpty()) {
            m.remove(u"why"_s);
            list << m;
        }
    }
    return list;
}
