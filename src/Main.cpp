/*
 *  SPDX-FileCopyrightText: 2019 David Redondo <kde@david-redondo.de>
 *  SPDX-FileCopyrightText: 2015 Boudhayan Gupta <bgupta@kde.org>
 *
 *  SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "Config.h"
#include "ShortcutActions.h"
#include "SpectacleCore.h"
#include "StartupClock.h"
#include "CommandLineOptions.h"
#include "SpectacleDBusAdapter.h"
#include "AgentService.h"
#include "ScreenShotEffect.h"
#include "settings.h"

#include <QScrollArea>
#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDir>
#include <QFontDatabase>
#include <QIcon>
#include <QSessionManager>

#include <QFile>
#include <QStandardPaths>
#include "Gui/SettingsDialog/SettingsDialog.h"
#include <QTimer>
#include "TestScript.h"
#include "Gui/PinWindow.h"
#include <KAboutData>
#include <KConfigGroup>
#include <KSharedConfig>
#include <KCrash>
#include <KDBusService>
#include <KLocalizedString>
#include <KMessageBox>
#include <KWindowSystem>

#include <QtQml/qqmlextensionplugin.h>

// Ectolens's own copy of KQuickImageEditor (3rdparty/kquickimageeditor) is a static QML module
Q_IMPORT_QML_PLUGIN(KQuickImageEditorPlugin)

using namespace Qt::StringLiterals;

int main(int argc, char **argv)
{
    // set up the application

    QCoreApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
    QIcon::setFallbackThemeName(u"breeze"_s);
    QApplication app(argc, argv);
    // Ectolens: times each step of a capture's start (StartupClock.h)
    StartupClock::start();

    // Ectolens: Geist and Geist Mono ship inside the app for the Ecto look
    // (src/fonts/README.md; SIL Open Font License 1.1).
    for (const auto &font : {u":/fonts/Geist-Regular.ttf"_s, u":/fonts/Geist-Medium.ttf"_s, u":/fonts/Geist-SemiBold.ttf"_s,
                             u":/fonts/GeistMono-Regular.ttf"_s, u":/fonts/GeistMono-Medium.ttf"_s}) {
        if (QFontDatabase::addApplicationFont(font) < 0) {
            qWarning() << "Ectolens could not load" << font;
        }
    }

    // Ectolens: bring a Spectacle user's settings along once. Ectolens keeps its
    // own file afterwards, so the two apps never change each other's settings.
    {
        const QString configDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
        const QString ours = configDir + u"/ectolensrc"_s;
        const QString theirs = configDir + u"/spectaclerc"_s;
        if (!QFile::exists(ours) && QFile::exists(theirs)) {
            QFile::copy(theirs, ours);
        }
    }

    // Ectolens: Spectacle's single "after taking a screenshot, copy…" choice became After
    // Capture's Copy to clipboard row (design proposal 2). Carry an old choice over once.
    {
        KConfigGroup general(KSharedConfig::openConfig(u"ectolensrc"_s), u"General"_s);
        if (general.hasKey("clipboardGroup")) {
            const QString old = general.readEntry("clipboardGroup", QString());
            // KConfig writes enums by name; very old files hold the number
            if (old == u"PostScreenshotDoNothing"_s || old == u"0"_s) {
                general.writeEntry("afterCaptureCopy", false);
            } else if (old == u"PostScreenshotCopyLocation"_s || old == u"2"_s) {
                general.writeEntry("afterCaptureCopy", true);
                general.writeEntry("afterCaptureCopyWhat", u"CopyTheFilePath"_s);
            } else {
                general.writeEntry("afterCaptureCopy", true);
            }
            general.deleteEntry("clipboardGroup");
            general.sync();
        }
    }

    KLocalizedString::setApplicationDomain(QByteArrayLiteral("spectacle"));
    QCoreApplication::setOrganizationDomain(u"ectonomic.com"_s); // D-Bus name com.ectonomic.ectolens

    KAboutData aboutData(u"ectolens"_s,
                         i18n("Ectolens"),
                         QStringLiteral(SPECTACLE_VERSION),
                         i18n("Screenshots and screen recordings"),
                         KAboutLicense::GPL_V2,
                         i18n("© 2015 Boudhayan Gupta and the KDE Spectacle authors; Ectolens changes © 2026"));
    aboutData.addAuthor(u"Boudhayan Gupta"_s, {}, u"bgupta@kde.org"_s);
    aboutData.addAuthor(u"David Redondo"_s, {}, u"kde@david-redondo.de"_s);
    aboutData.addAuthor(u"Noah Davis"_s, {}, u"noahadvs@gmail.com"_s);
    // Ectolens: KAboutData's own domain decides the app id Plasma and KWin see
    // (it was org.kde.ectolens, which matched no desktop file, so KWin refused
    // the screenshot permission and Print Screen showed nothing).
    aboutData.setOrganizationDomain("ectonomic.com");
    aboutData.setDesktopFileName(u"com.ectonomic.ectolens"_s);
    aboutData.setTranslator(i18nc("NAME OF TRANSLATORS", "Your names"), i18nc("EMAIL OF TRANSLATORS", "Your emails"));
    KAboutData::setApplicationData(aboutData);
    // On Wayland, KWin takes the icon from the desktop file (Icon=ectolens), as it does for Konsole. Handing it a
    // window icon instead sent the bright lime edge pixels through a path that showed them red in the title bar
    // (seen off-screen 2026-11-01: (209,4,63) at the splat's edge, none from Qt's own render of the same SVG).
    if (!QGuiApplication::platformName().startsWith(u"wayland"_s)) {
        app.setWindowIcon(QIcon::fromTheme(u"ectolens"_s, QIcon::fromTheme(u"spectacle"_s)));
    }

    KCrash::initialize();

    QCommandLineParser commandLineParser;
    aboutData.setupCommandLine(&commandLineParser);
    commandLineParser.addOptions(CommandLineOptions::self()->allOptions);

    // first parsing for help-about
    commandLineParser.process(app.arguments());
    aboutData.processCommandLine(&commandLineParser);

    // BUG: https://bugs.kde.org/show_bug.cgi?id=451842
    // We currently don't support desktop environments besides KDE Plasma on Wayland
    // because we have to rely on KWin's DBus API.
    if (KWindowSystem::isPlatformWayland() && !ScreenShotEffect::isLoaded()) {
        auto message = i18n("On Wayland, Ectolens requires KDE Plasma's KWin compositor, which does not seem to be available. Use Ectolens on KDE Plasma, or use a different screenshot tool.");
        qWarning().noquote() << message;
        if (commandLineParser.isSet(CommandLineOptions::self()->background)
            || commandLineParser.isSet(CommandLineOptions::self()->dbus)) {
            // Return early if not in GUI mode.
            return 1;
        } else {
            KMessageBox::error(nullptr, message);
        }
    }

    // Prevent session manager from restoring the app on start up.
    // https://bugs.kde.org/show_bug.cgi?id=430411
    auto disableSessionManagement = [](QSessionManager &sm) {
        sm.setRestartHint(QSessionManager::RestartNever);
    };
    QObject::connect(&app, &QGuiApplication::commitDataRequest, disableSessionManagement);
    QObject::connect(&app, &QGuiApplication::saveStateRequest, disableSessionManagement);

    // Ectolens test hook: render a Settings page off-screen into a picture and exit, so the
    // page can be checked without opening a window. ECTOLENS_SETTINGS_SHOT_PAGE picks the page:
    // recording (the default), sound (Recording, scrolled to its Sound section), appearance or
    // general. ECTOLENS_SETTINGS_SHOT_HOLD keeps the
    // dialog showing that many milliseconds first (the README's film takes it with its frame).
    if (const QString shot = qEnvironmentVariable("ECTOLENS_SETTINGS_SHOT"); !shot.isEmpty()) {
        SpectacleCore::instance();
        auto dialog = new SettingsDialog;
        const QString page = qEnvironmentVariable("ECTOLENS_SETTINGS_SHOT_PAGE");
        if (page == u"appearance"_s) {
            dialog->showAppearancePage();
        } else if (page == u"general"_s) {
            // the first page
        } else {
            dialog->showRecordingPage();
        }
        dialog->resize(qEnvironmentVariableIntValue("ECTOLENS_SETTINGS_SHOT_WIDTH") ?: 1000, qEnvironmentVariableIntValue("ECTOLENS_SETTINGS_SHOT_HEIGHT") ?: 1400);
        dialog->show();
        if (page == u"sound"_s) {
            // the page scrolled so its Sound section shows whole
            QTimer::singleShot(300, dialog, [dialog] {
                if (auto box = dialog->findChild<QWidget *>(u"kcfg_videoSeparateAudioTracks"_s)) {
                    for (QWidget *w = box->parentWidget(); w; w = w->parentWidget()) {
                        if (auto area = qobject_cast<QScrollArea *>(w)) {
                            area->ensureWidgetVisible(box, 0, 40);
                            break;
                        }
                    }
                }
            });
        }
        QTimer::singleShot(qEnvironmentVariableIntValue("ECTOLENS_SETTINGS_SHOT_HOLD") ?: 800, dialog, [dialog, shot] {
            dialog->grab().save(shot);
            QCoreApplication::quit();
        });
        return app.exec();
    }

    // If the new instance command line option has been specified,
    // use this alternative path for executing Spectacle.
    if (commandLineParser.isSet(CommandLineOptions::self()->newInstance)) {
        auto spectacleCore = SpectacleCore::instance();
    StartupClock::mark(u"core"_s);

        QObject::connect(qApp, &QApplication::aboutToQuit, Settings::self(), &Settings::save);
        QObject::connect(spectacleCore, &SpectacleCore::allDone, &app, &QCoreApplication::quit, Qt::QueuedConnection);

        // fire it up
        spectacleCore->activate(app.arguments(), QDir::currentPath());

        return app.exec();
    }

    // With the StartupOption::Unique flag, this process will exit during the construction of
    // KDBusService if Spectacle has already been registered.
    // This object does not need a parent since it will be deleted when it falls out of scope.
    KDBusService service(KDBusService::Unique);
    StartupClock::mark(u"service"_s);

    auto spectacleCore = SpectacleCore::instance();

    // the shortcut's message: StartupClock's "press" (started by D-Bus, Ectolens waits for it)
    QObject::connect(&service, &KDBusService::activateRequested, spectacleCore, [] {
        StartupClock::mark(u"press"_s);
    });
    QObject::connect(&service, &KDBusService::activateActionRequested, spectacleCore, [] {
        StartupClock::mark(u"press"_s);
    });
    QObject::connect(&service, &KDBusService::activateRequested, spectacleCore, &SpectacleCore::activate);
    QObject::connect(&service, &KDBusService::activateActionRequested, spectacleCore, &SpectacleCore::activateAction);

    QObject::connect(&app, &QCoreApplication::aboutToQuit, Settings::self(), &Settings::save);
    // Ectolens: pins outlive the rest; done with them still up, the app quits once the last closes.
    QObject::connect(spectacleCore, &SpectacleCore::allDone, &app, [] {
        if (PinWindow::count() > 0) {
            PinWindow::quitAfterLast();
        } else {
            QCoreApplication::quit();
        }
    }, Qt::QueuedConnection);

    // create the dbus connections
    SpectacleDBusAdapter *dbusAdapter = new SpectacleDBusAdapter(spectacleCore);
    QObject::connect(spectacleCore, &SpectacleCore::dbusScreenshotFailed, dbusAdapter, &SpectacleDBusAdapter::ScreenshotFailed);
    QObject::connect(spectacleCore, &SpectacleCore::dbusRecordingFailed, dbusAdapter, &SpectacleDBusAdapter::RecordingFailed);
    QObject::connect(ExportManager::instance(),
                     &ExportManager::imageExported,
                     spectacleCore,
                     [dbusAdapter](const ExportManager::Actions &actions, const QUrl &url) {
                         Q_UNUSED(actions)
                         Q_EMIT dbusAdapter->ScreenshotTaken(url.toLocalFile());
                     });
    QObject::connect(ExportManager::instance(),
                     &ExportManager::videoExported,
                     spectacleCore,
                     [dbusAdapter](const ExportManager::Actions &actions, const QUrl &url) {
                         Q_UNUSED(actions)
                         Q_EMIT dbusAdapter->RecordingTaken(url.toLocalFile());
                     });
    QDBusConnection::sessionBus().registerObject(u"/"_s, spectacleCore);
    // Ectolens: what an AI agent may ask, through ectolens-mcp (off until Settings › Model Context Protocol (MCP))
    QDBusConnection::sessionBus().registerObject(u"/Agent"_s, new AgentService(spectacleCore), QDBusConnection::ExportAllSlots);
    QDBusConnection::sessionBus().registerService(u"com.ectonomic.Ectolens"_s);

    // fire it up
    spectacleCore->activate(app.arguments(), QDir::currentPath());
    TestScript::startIfRequested();

    return app.exec();
}
