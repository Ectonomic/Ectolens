/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 *
 * Which of KDE's share services Ectolens offers (src/Gui/ShareServices.cpp), against KDE's
 * real share services: the ones it retires are never offered, Send via Email only with an
 * email app set up, Send to Device only with a device paired in KDE Connect. KDE Connect is
 * a stand-in on a private session bus (ctest runs this under dbus-run-session), so the real
 * KDE Connect and its devices are never asked; the test refuses to run on a bus that has one.
 */
#include "Gui/ShareServices.h"

#include <KLocalizedString>
#include <KSycoca>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTest>
#include <optional>

using namespace Qt::StringLiterals;

class FakeKdeConnect : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kdeconnect.daemon")
public:
    QStringList paired;
public Q_SLOTS:
    QStringList devices(bool onlyReachable, bool onlyPaired)
    {
        Q_UNUSED(onlyReachable)
        return onlyPaired ? paired : paired + QStringList{u"not-paired"_s};
    }
};

class ShareServicesTest : public QObject
{
    Q_OBJECT
    FakeKdeConnect m_kdeconnect;

    static QStringList ids(const QVariantList &list)
    {
        QStringList out;
        for (const auto &v : list) {
            out << v.toMap().value(u"id"_s).toString();
        }
        return out;
    }
    static QUrl picture()
    {
        return QUrl::fromLocalFile(QDir::tempPath() + u"/share-test.png"_s);
    }
    static QUrl video()
    {
        return QUrl::fromLocalFile(QDir::tempPath() + u"/share-test.mp4"_s);
    }
    enum Mail { NoMailApp, MailApp, HiddenHelper, ProgramGone };
    std::optional<Mail> m_mailApp;
    void setMail(Mail kind)
    {
        if (m_mailApp == kind) {
            return;
        }
        m_mailApp = kind;
        // KService notices changed files by their folders' times, which count whole seconds,
        // and looks again at most every 1.5 seconds
        QTest::qSleep(1600);
        const QString apps = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
        const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
        QDir().mkpath(apps);
        QDir().mkpath(config);
        QFile::remove(apps + u"/test-mail.desktop"_s);
        QFile::remove(config + u"/mimeapps.list"_s);
        if (kind != NoMailApp) {
            QFile d(apps + u"/test-mail.desktop"_s);
            QVERIFY(d.open(QIODevice::WriteOnly));
            d.write("[Desktop Entry]\nType=Application\nName=Test Mail\nMimeType=x-scheme-handler/mailto;\n");
            d.write(kind == ProgramGone ? "Exec=ectolens-no-such-mail-app %u\n" : "Exec=true %u\n");
            if (kind == HiddenHelper) {
                d.write("NoDisplay=true\n"); // as KDE 4's kmailservice
            }
            d.close();
            QFile m(config + u"/mimeapps.list"_s);
            QVERIFY(m.open(QIODevice::WriteOnly));
            m.write("[Default Applications]\nx-scheme-handler/mailto=test-mail.desktop;\n");
            m.close();
        }
        KSycoca::self()->ensureCacheValid();
    }

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        KLocalizedString::setApplicationDomain(QByteArrayLiteral("spectacle"));
        QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation));
        QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation));
        auto bus = QDBusConnection::sessionBus();
        QVERIFY2(bus.isConnected(), "needs a session bus: run under dbus-run-session");
        QVERIFY2(!bus.interface()->isServiceRegistered(u"org.kde.kdeconnect"_s),
                 "a real KDE Connect is on this bus: run under dbus-run-session, never on the desktop's bus");
        QVERIFY(bus.registerObject(u"/modules/kdeconnect"_s, &m_kdeconnect, QDBusConnection::ExportAllSlots));
        QVERIFY(bus.registerService(u"org.kde.kdeconnect"_s));
        // the test sees KDE's services at all, or every check below would pass on nothing
        QVERIFY2(!ShareServices::all(picture()).isEmpty(), "no KDE share services installed (Purpose's plugins)");
    }

    // Imgur, YouTube and Save as are deprecated (2026-11-12); Copy to clipboard
    // was left out before (After Capture has its own row)
    void retiredNeverOffered_data()
    {
        QTest::addColumn<QString>("id");
        for (auto id : {"imgurplugin", "youtubeplugin", "saveasplugin", "clipboardplugin"}) {
            QTest::newRow(id) << QString::fromLatin1(id);
        }
    }
    void retiredNeverOffered()
    {
        QFETCH(QString, id);
        QVERIFY(ShareServices::isRetired(id));
        QVERIFY(!ShareServices::retiredBecause(id).isEmpty());
        m_kdeconnect.paired = {u"phone"_s};
        setMail(MailApp);
        for (const QUrl &file : {picture(), video()}) {
            QVERIFY2(!ids(ShareServices::offered(file)).contains(id), qPrintable(id + u" offered for "_s + file.fileName()));
        }
    }

    // everything KDE has that Ectolens doesn't leave out on purpose is offered, given an email
    // app and a paired device: the filter takes away only what it names
    void everythingElseOffered()
    {
        m_kdeconnect.paired = {u"phone"_s};
        setMail(MailApp);
        for (const QUrl &file : {picture(), video()}) {
            const QStringList offered = ids(ShareServices::offered(file));
            for (const auto &v : ShareServices::all(file)) {
                const QString id = v.toMap().value(u"id"_s).toString();
                QCOMPARE(offered.contains(id), !ShareServices::isRetired(id));
            }
        }
    }

    void emailNeedsAMailApp()
    {
        m_kdeconnect.paired = {u"phone"_s};
        if (!ids(ShareServices::all(picture())).contains(u"emailplugin"_s)) {
            QSKIP("KDE's email share service isn't installed");
        }
        for (Mail none : {NoMailApp, HiddenHelper, ProgramGone}) {
            setMail(none);
            QVERIFY2(!ShareServices::hasMailApp(), qPrintable(u"mail kind %1"_s.arg(int(none))));
            QVERIFY(!ids(ShareServices::offered(picture())).contains(u"emailplugin"_s));
        }
        setMail(MailApp);
        QVERIFY(ShareServices::hasMailApp());
        QVERIFY(ids(ShareServices::offered(picture())).contains(u"emailplugin"_s));
    }

    void sendToDeviceNeedsAPairedDevice()
    {
        setMail(MailApp);
        if (!ids(ShareServices::all(picture())).contains(u"kdeconnectplugin"_s)) {
            QSKIP("KDE Connect's share service isn't installed");
        }
        m_kdeconnect.paired = {};
        QVERIFY(!ShareServices::hasPairedDevice());
        QVERIFY(!ids(ShareServices::offered(picture())).contains(u"kdeconnectplugin"_s));
        m_kdeconnect.paired = {u"phone"_s};
        QVERIFY(ShareServices::hasPairedDevice());
        QVERIFY(ids(ShareServices::offered(picture())).contains(u"kdeconnectplugin"_s));
    }

    void cleanupTestCase()
    {
        setMail(NoMailApp);
    }
};

QTEST_GUILESS_MAIN(ShareServicesTest)
#include "ShareServicesTest.moc"
