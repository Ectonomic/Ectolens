/*
 *  SPDX-FileCopyrightText: 2019 David Redondo <kde@david-redondo.de>
 *  SPDX-FileCopyrightText: 2015 Boudhayan Gupta <bgupta@kde.org>
 *  SPDX-FileCopyrightText: 2025 Jhair Paris <dev@jhairparis.com>
 *
 *  SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "SpectacleCore.h"
#include "StartupClock.h"
#include "Gui/CaptureTools.h"
#include "CaptureModeModel.h"
#include "CommandLineOptions.h"
#include "Config.h"
#include "ExportManager.h"
#include "Geometry.h"
#include "Gui/CaptureWindow.h"
#include "Gui/Selection.h"
#include "Gui/SelectionEditor.h"
#include "Gui/FrameHistory.h"
#include "Platforms/WindowPicker.h"
#include <QPainter>
#include "Platforms/ImagePlatformKWin.h"
#include "Gui/SpectacleWindow.h"
#include "Gui/InlineMessageModel.h"
#include "ImageMetaData.h"
#include "OcrManager.h"
#if WITH_X11
#include "Platforms/ImagePlatformXcb.h"
#endif
#include "Platforms/PlatformLoader.h"
#include "RecordingModeModel.h"
#include "ShortcutActions.h"
#include "TestScript.h"
#include "PlasmaVersion.h"
// generated
#include "settings.h"

#include <KFormat>
#include <KGlobalAccel>
#include <KIO/OpenUrlJob>
#include <KLocalizedString>
#include <KMessageBox>
#include <KNotification>
#include <KStatusNotifierItem>
#include <KWindowSystem>
#if WITH_X11
#include <KX11Extras>
#endif
#include <LayerShellQt/Shell>
#include <LayerShellQt/Window>

#include <KLocalizedQmlContext>
#include <QApplication>
#include <QClipboard>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QCursor>
#include <QDateTime>
#include <QMetaEnum>
#include <QDir>
#include <QStandardPaths>
#include <QDrag>
#include <QKeySequence>
#include <QMetaObject>
#include <QMimeData>
#include <QMovie>
#include <QObject>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QScopedPointer>
#include <QScreen>
#include <QSystemTrayIcon>
#include <QTemporaryFile>
#include <QTextStream>
#include <QTimer>
#include <QtMath>
#include <qobjectdefs.h>

using namespace Qt::StringLiterals;

SpectacleCore *SpectacleCore::s_self = nullptr;
static std::unique_ptr<KStatusNotifierItem> s_systemTrayIcon;

static QList<KNotification *> notifications;

SpectacleCore::SpectacleCore(QObject *parent)
    : QObject(parent)
{
    // Timer to prevent lots of extra rendering to images
    m_annotationSyncTimer = std::make_unique<QTimer>();
    m_annotationSyncTimer->setInterval(400);
    m_annotationSyncTimer->setSingleShot(true);

    m_delayAnimation = std::make_unique<QVariantAnimation>(this);
    m_delayAnimation->setStartValue(0.0);
    m_delayAnimation->setEndValue(1.0);
    m_delayAnimation->setDuration(1);
    m_delayAnimation->setCurrentTime(0);
    auto delayAnimation = m_delayAnimation.get();
    // We need to reset this on start in case a previous instance
    // didn't reset these before it closed or crashed.
    unityLauncherUpdate({
        {u"progress-visible"_s, false},
        {u"progress"_s, 0}
    });
    using State = QVariantAnimation::State;
    auto onStateChanged = [this](State newState, State oldState) {
        Q_UNUSED(oldState)
        if (newState == State::Running) {
            unityLauncherUpdate({{u"progress-visible"_s, true}});
        } else if (newState == State::Stopped) {
            unityLauncherUpdate({{u"progress-visible"_s, false}});
            m_delayAnimation->setCurrentTime(0);
        }
    };
    auto onValueChanged = [this](const QVariant &value) {
        Q_EMIT captureTimeRemainingChanged();
        Q_EMIT captureProgressChanged();
        unityLauncherUpdate({{u"progress"_s, value.toReal()}});
        const auto windows = SpectacleWindow::instances();
        if (m_delayAnimation->state() != State::Stopped && !windows.isEmpty()) {
            if (captureTimeRemaining() <= 500 && windows.constFirst()->isVisible()) {
                SpectacleWindow::setVisibilityForAll(QWindow::Hidden);
            }
            SpectacleWindow::setTitleForAll(SpectacleWindow::Timer);
        }
    };
    auto onFinished = [this]() {
        m_imagePlatform->doGrab(ImagePlatform::ShutterMode::Immediate, m_lastGrabMode, m_lastIncludePointer, m_lastIncludeDecorations, m_lastIncludeShadow);
    };
    QObject::connect(delayAnimation, &QVariantAnimation::stateChanged,
                     this, onStateChanged, Qt::QueuedConnection);
    QObject::connect(delayAnimation, &QVariantAnimation::valueChanged,
                     this, onValueChanged, Qt::QueuedConnection);
    QObject::connect(delayAnimation, &QVariantAnimation::finished,
                     this, onFinished, Qt::QueuedConnection);

    m_imagePlatform = loadImagePlatform();
    m_videoPlatform = loadVideoPlatform();
    // Ectolens: start learning where windows are now, so pointing at one works
    // the moment the overlay opens.
    if (KWindowSystem::isPlatformWayland()) {
        WindowPicker::instance();
    }
    auto imagePlatform = m_imagePlatform.get();
    m_annotationDocument = std::make_unique<AnnotationDocument>();

    // essential connections
    // Ectolens: an agent's capture waiting on Hide Private Info's reading of its frame goes when the
    // reading ends with its blurs on. When the text reader did not start, the overlay's note says
    // so and the person decides again (a second Send sends it as it is, as the dialog allows).
    connect(CaptureTools::instance(), &CaptureTools::hidingPrivateInfoChanged, this, [this] {
        auto tools = CaptureTools::instance();
        if (!m_agentSendWhenRead || tools->hidingPrivateInfo()) {
            return;
        }
        m_agentSendWhenRead = false;
        if (m_agent && tools->privateInfoReaderRan()) {
            // after this signal has reached everyone: sending closes the overlay, whose items listen to it too
            QTimer::singleShot(0, this, [this] {
                if (m_agent) {
                    SelectionEditor::instance()->acceptSelection();
                }
            });
        }
    });
    connect(SelectionEditor::instance(), &SelectionEditor::accepted, this, [this](const QRectF &rect, const ExportManager::Actions &actions) {
        m_returnToViewer = m_startMode == StartMode::Gui;
        if (m_videoMode) {
            // Ectolens: a recording follows After Capture too: the window opens after it only
            // with Open with › Ectolens checked, or when the window was open before.
            m_captureTitle = captureTitle(rect);
            m_afterCapture = {};
            m_afterCapture.pending = true;
            m_afterCapture.open = Settings::afterCaptureOpen();
            m_afterCapture.openApp = Settings::afterCaptureOpenApp();
            m_afterCapture.showInFolder = Settings::afterCaptureShowInFolder() && Settings::autoSaveImage();
            m_afterCapture.share = Settings::afterCaptureShare() && !Settings::afterCaptureShareTarget().isEmpty();
            m_afterCapture.shareTarget = Settings::afterCaptureShareTarget();
            if (m_startMode == StartMode::Gui) {
                m_returnToViewer = m_captureFromWindow || (m_afterCapture.open && m_afterCapture.openApp.isEmpty());
            }
            const auto captureWindows = CaptureWindow::instances();
            SpectacleWindow::setVisibilityForAll(QWindow::Hidden);
            for (auto captureWindow : captureWindows) {
                // Destroy the QPlatformWindow so we can change the window behavior.
                // The QPlatformWindow will be recreated when the window is shown again.
                captureWindow->destroy();
                // Ectolens: clicks go through to the apps being recorded everywhere
                // except the recording toolbar, which sets its own input area.
                captureWindow->setInputRect({});
                captureWindow->setFlag(Qt::WindowStaysOnTopHint, true);
                if (auto window = LayerShellQt::Window::get(captureWindow)) {
                    using namespace LayerShellQt;
                    window->setCloseOnDismissed(true);
                    window->setLayer(Window::LayerOverlay);
                    auto anchors = Window::Anchors::fromInt(Window::AnchorTop | Window::AnchorBottom | Window::AnchorLeft | Window::AnchorRight);
                    window->setAnchors(anchors);
                    // -1 means this window should not make way for other surfaces such as panels.
                    window->setExclusiveZone(-1);
                    window->setKeyboardInteractivity(Window::KeyboardInteractivityNone);
                    window->setScreen(captureWindow->screenToFollow());
                }
            }
            SpectacleWindow::setVisibilityForAll(QWindow::FullScreen);
            // deleteWindows();
            // showViewerIfGuiMode(true);
            bool includePointer = m_cliOptions[CommandLineOptions::Pointer];
            includePointer |= m_startMode != StartMode::Background && Settings::videoIncludePointer();
            ExportManager::instance()->updateTimestamp();
            const auto &output = m_outputUrl.isLocalFile() ? videoOutputUrl() : QUrl();
            static const auto rectKey = u"rect"_s;
            // Ectolens: a frame that is still exactly a window taken by clicking it
            // records that window, so the recording follows it if it moves.
            if (const QString uuid = SelectionEditor::instance()->frameWindowUuid(); !uuid.isEmpty()
                && (m_videoPlatform->supportedRecordingModes() & VideoPlatform::Window)) {
                m_videoPlatform->startRecording(output,
                                                VideoPlatform::Window,
                                                {{u"uuid"_s, uuid}, {u"x"_s, rect.x()}, {u"y"_s, rect.y()}, {u"width"_s, rect.width()}, {u"height"_s, rect.height()}},
                                                includePointer);
            } else {
                m_videoPlatform->startRecording(output, VideoPlatform::Region, {{rectKey, rect}}, includePointer);
            }
            if (m_agent && m_agent->kind == u"record"_s) {
                // an agent's recording: saved where recordings go, its file given to the agent at
                // record_stop; the window doesn't open for it
                m_afterCapture = {};
                m_returnToViewer = false;
                m_agentRecording = true;
                m_agentLastRecording.clear();
                m_agent.reset();
                answerAgent({{u"status"_s, u"recording"_s}});
                Q_EMIT agentChanged();
            }
        } else if (m_agent && m_agent->kind == u"record"_s) {
            // Enter on a recording an agent asked for: Record, as its Send button does
            recordSelection();
        } else if (m_agent && m_agent->kind == u"capture"_s) {
            // Ectolens: the person pressed Send (or Enter) on a capture an agent asked for: it
            // goes to the agent alone, with no After Capture. A frame moved or grown past what
            // Hide Private Info read (Adjust First) is read first, and goes once its blurs are on.
            auto tools = CaptureTools::instance();
            if (m_agentBlur && !tools->privateInfoReadFor(rect)) {
                tools->hidePrivateInfo(rect);
                if (tools->hidingPrivateInfo()) {
                    m_agentSendWhenRead = true;
                    TestScript::report(u"ECTOLENS-AGENT reading"_s);
                    return;
                }
                // nothing there to read (no picture under the frame): it goes as it is
            }
            sendToAgent(rect);
        } else {
            // Ectolens: Enter and Capture run the After Capture list; the other keys
            // (Ctrl+C, Ctrl+S, Ctrl+Shift+S) and Extract Text do only what they say.
            const bool afterCapture = !(actions & ExportManager::AnyAction) && !m_ocrExportInProgress;
            if (afterCapture && Settings::afterCaptureShare() && Settings::afterCaptureShareTarget().isEmpty() && !m_shareAsked) {
                // Share asks where each time: the overlay shows the services over Capture first.
                Q_EMIT shareTargetNeeded();
                return;
            }
            m_captureTitle = captureTitle(rect);
            m_afterCapture = {};
            if (afterCapture) {
                m_afterCapture.pending = true;
                m_afterCapture.open = Settings::afterCaptureOpen();
                m_afterCapture.openApp = Settings::afterCaptureOpenApp();
                m_afterCapture.showInFolder = Settings::afterCaptureShowInFolder() && Settings::autoSaveImage();
                m_afterCapture.share = Settings::afterCaptureShare() && (!m_shareAsked || !m_shareOnce.isEmpty());
                m_afterCapture.shareTarget = m_shareAsked ? m_shareOnce : Settings::afterCaptureShareTarget();
            }
            m_shareAsked = false;
            m_shareOnce.clear();
            CaptureTools::instance()->revertResizedWindow();
            SpectacleWindow::setVisibilityForAll(QWindow::Hidden);
            deleteWindows();
            m_annotationDocument->cropCanvas(rect);
            syncExportImage();
            auto exportActions = actions & ExportManager::AnyAction ? actions : autoExportActions();
            bool ocrRun = false;
            if (afterCapture && m_startMode != StartMode::Background) {
                // After a Print Screen capture the overlay closes instead of opening the
                // Ectolens window, unless Open with › Ectolens is checked or the window was
                // open when the capture started (design proposal 2).
                m_returnToViewer = m_captureFromWindow || (m_afterCapture.open && m_afterCapture.openApp.isEmpty());
            }
            if (m_ocrExportInProgress) {
                if (Settings::closeAfterOcr()) {
                    exportActions.setFlag(ExportManager::Action::Save, false);
                    exportActions.setFlag(ExportManager::Action::CopyPath, false);
                    m_quitAfterOcr = true;
                }
                exportActions.setFlag(ExportManager::Action::CopyImage, false);
                m_ocrExportInProgress = false;
                ocrRun = true;
            }
            const bool willQuit = exportActions.testAnyFlags(ExportManager::AnyAction) //
                && exportActions.testFlag(ExportManager::UserAction) //
                && Settings::quitAfterSaveCopyExport();
            m_returnToViewer &= !willQuit;
            showViewerIfGuiMode();
            SpectacleWindow::setTitleForAll(SpectacleWindow::Unsaved);
            ExportManager::instance()->scanQRCode();
            // Extract Text speaks in its own notification: with nothing left to copy or save,
            // don't export (that would send a "not copied or saved" one too)
            if (!ocrRun || exportActions & ExportManager::AnyAction) {
                ExportManager::instance()->exportImage(exportActions, outputUrl());
            }
        }
    });

    connect(imagePlatform, &ImagePlatform::newScreenshotTaken, this, [this](const QImage &image){
        StartupClock::finish(u"picture-without-overlay"_s);
        InlineMessageModel::instance()->clear();
        CaptureTools::instance()->setSavedUrl({});
        m_editExistingUrl.clear();
        m_cliOptions[CommandLineOptions::EditExisting] = false;
        m_annotationDocument->clearAnnotations();
        m_annotationDocument->setBaseImage(image);
        setExportImage(image);
        ExportManager::instance()->updateTimestamp();
        m_returnToViewer = true;
        // Ectolens: a capture without the overlay (full screen, active window) runs After Capture too.
        m_afterCapture = {};
        if (m_startMode != StartMode::Background && !m_apiCapture) {
            const QSize px = image.size();
            QString what = CaptureModeModel::captureModeLabel(toCaptureMode(m_lastGrabMode));
            const QString windowTitle = ImageMetaData::windowTitle(image);
            if (!windowTitle.isEmpty()) {
                const auto parts = windowTitle.split(u" — "_s);
                what = parts.last().trimmed();
            }
            m_captureTitle = i18nc("@title %1 what was taken, %2×%3 its size", "%1 %2×%3", what, QString::number(px.width()), QString::number(px.height()));
            m_afterCapture.pending = true;
            m_afterCapture.open = Settings::afterCaptureOpen();
            m_afterCapture.openApp = Settings::afterCaptureOpenApp();
            m_afterCapture.showInFolder = Settings::afterCaptureShowInFolder() && Settings::autoSaveImage();
            m_afterCapture.share = Settings::afterCaptureShare() && !Settings::afterCaptureShareTarget().isEmpty();
            m_afterCapture.shareTarget = Settings::afterCaptureShareTarget();
            m_returnToViewer = m_captureFromWindow || (m_afterCapture.open && m_afterCapture.openApp.isEmpty())
                || (m_startMode == StartMode::Gui && !m_afterCapture.pending);
        }
        showViewerIfGuiMode();
        SpectacleWindow::setTitleForAll(SpectacleWindow::Unsaved);
        ExportManager::instance()->scanQRCode();
        ExportManager::instance()->exportImage(autoExportActions(), outputUrl());
        setVideoMode(false);
    });
    connect(imagePlatform, &ImagePlatform::newCroppableScreenshotTaken, this, [this](const QImage &image) {
        StartupClock::mark(u"picture"_s);
        m_retaking = false;
        InlineMessageModel::instance()->clear();
        CaptureTools::instance()->setSavedUrl({});
        m_editExistingUrl.clear();
        m_cliOptions[CommandLineOptions::EditExisting] = false;
        setVideoMode(false);
        m_annotationDocument->clearAnnotations();
        m_annotationDocument->setBaseImage(image);
        setExportImage(image);
        ExportManager::instance()->updateTimestamp();
        initCaptureWindows(CaptureWindow::Image);
        StartupClock::mark(u"windows"_s);
        SpectacleWindow::setTitleForAll(SpectacleWindow::Unsaved);
        SpectacleWindow::setVisibilityForAll(QWindow::FullScreen);
        StartupClock::mark(u"shown"_s);
        // Ectolens: Open QR Code appears when the frame holds a code.
        CaptureTools::instance()->scanFrozenPicture();
        StartupClock::mark(u"qr-scan"_s);
        // the overlay is drawn once every screen's window has swapped its first frame
        auto waiting = std::make_shared<int>(int(m_captureWindows.size()));
        for (const auto &window : m_captureWindows) {
            connect(window.get(), &QQuickWindow::frameSwapped, this, [waiting] {
                if (--*waiting == 0) {
                    StartupClock::overlayDrawn();
                }
            }, Qt::SingleShotConnection);
        }
        // SelectionEditor::reset has to be delayed until after all the DPRs are correct on Wayland.
        connect(m_captureWindows.front().get(), &CaptureWindow::allExposed, //
                SelectionEditor::instance(), &SelectionEditor::reset, Qt::SingleShotConnection);
        if (m_agent) {
            // an agent's frame, once the reset above has cleared the overlay's (connected after it)
            connect(m_captureWindows.front().get(), &CaptureWindow::allExposed, this, &SpectacleCore::frameForAgent, Qt::SingleShotConnection);
        }
    });

    // The behavior happens to be very similar right now, so we use a shared base function
    auto onScreenshotOrRecordingFailed = [this](const QString &message,
                                                QString uiMessage,
                                                void (SpectacleCore::*dBusFailedSignal)(const QString &)) {
        if (!message.isEmpty()) {
            uiMessage = uiMessage % u"\n"_s % message;
        }
        const auto &windows = qGuiApp->allWindows();
        const bool hasVisibleWindow = std::any_of(windows.cbegin(), windows.cend(), [](auto *window){
            // Is visible and is a normal window or dialog (no tooltips or menus)
            const auto typeFlags = window->flags() & Qt::WindowType_Mask;
            return window->isVisible() && (typeFlags == Qt::Window || typeFlags == Qt::Dialog);
        });
        switch (m_startMode) {
        case StartMode::Background:
            showErrorMessage(uiMessage);
            if (!hasVisibleWindow) {
                Q_EMIT allDone();
            }
            return;
        case StartMode::DBus: {
            showErrorMessage(uiMessage);
            Q_EMIT (this->*dBusFailedSignal)(message);
            if (!hasVisibleWindow) {
                Q_EMIT allDone();
            }
            return;
        }
        case StartMode::Gui: {
            if (!ViewerWindow::instance()) {
                InlineMessageModel::instance()->clear();
                if (hasVisibleWindow) {
                    showErrorMessage(uiMessage);
                    return;
                } else {
                    initViewerWindow(ViewerWindow::Dialog);
                }
            }
            qWarning().noquote() << message;
            auto viewer = ViewerWindow::instance();
            viewer->setVisible(true);
            InlineMessageModel::instance()->push(InlineMessageModel::Error, uiMessage);
            return;
        }
        }
    };
    connect(imagePlatform, &ImagePlatform::newScreenshotFailed, this, [this, onScreenshotOrRecordingFailed](const QString message) {
        m_retaking = false;
        CaptureTools::instance()->revertResizedWindow();
        auto uiMessage = i18nc("@info", "An error occurred while taking a screenshot.");
        onScreenshotOrRecordingFailed(message, uiMessage, &SpectacleCore::dbusScreenshotFailed);
    });
    connect(imagePlatform, &ImagePlatform::newScreenshotCanceled, this, [this]() {
        CaptureTools::instance()->revertResizedWindow();
        if (m_startMode != StartMode::Gui || !m_returnToViewer || isGuiNull()) {
            Q_EMIT allDone();
            return;
        }
        SpectacleWindow::setTitleForAll(SpectacleWindow::Previous);
        const auto windows = SpectacleWindow::instances();
        if (windows.empty()) {
            initViewerWindow(ViewerWindow::Viewer);
            return;
        }
        for (auto w : windows) {
            w->setVisible(true);
        }
    });

    auto videoPlatform = m_videoPlatform.get();
    connect(videoPlatform, &VideoPlatform::recordingStateChanged, this, [this, videoPlatform](VideoPlatform::RecordingState state) {
        // for the off-screen tests: each state with the wall clock, so a screenshot can be placed
        // in it (the input run shoots the screen between Stop and the saved file)
        TestScript::report(u"ECTOLENS-REC %1 %2"_s.arg(QString::fromLatin1(QMetaEnum::fromType<VideoPlatform::RecordingState>().valueToKey(int(state))))
                               .arg(QDateTime::currentMSecsSinceEpoch()));
        // Ectolens: pausing and resuming keep the tray icon and the recording
        // overlay; only the tooltip says which it is.
        if (state == VideoPlatform::RecordingState::Paused) {
            if (s_systemTrayIcon) {
                s_systemTrayIcon->setToolTipTitle(i18nc("@info:tooltip title for paused recording tray icon", "Ectolens is Paused"));
            }
            return;
        }
        if (state == VideoPlatform::RecordingState::Recording && s_systemTrayIcon) {
            s_systemTrayIcon->setToolTipTitle(videoPlatform->isRecordingAudio()
                                                  ? i18nc("@info:tooltip title for recording tray icon", "Ectolens is Recording with Sound")
                                                  : i18nc("@info:tooltip title for recording tray icon", "Ectolens is Recording"));
            return;
        }
        if (state == VideoPlatform::RecordingState::Recording) {
            static const auto recordingIcon = u":/icons/256-status-media-recording.webp"_s;
            static const auto recordingStartedIcon = u":/icons/256-status-media-recording-started.webp"_s;
            static const auto recordingPulseIcon = u":/icons/256-status-media-recording-pulse.webp"_s;
            s_systemTrayIcon = std::make_unique<KStatusNotifierItem>();
            s_systemTrayIcon->setStatus(KStatusNotifierItem::Active);
            s_systemTrayIcon->setCategory(KStatusNotifierItem::SystemServices);
            s_systemTrayIcon->setToolTipTitle(videoPlatform->isRecordingAudio()
                                                  ? i18nc("@info:tooltip title for recording tray icon", "Ectolens is Recording with Sound")
                                                  : i18nc("@info:tooltip title for recording tray icon", "Ectolens is Recording"));
            s_systemTrayIcon->setStandardActionsEnabled(false);
            connect(s_systemTrayIcon.get(), &KStatusNotifierItem::activateRequested, this, [] {
                SpectacleCore::instance()->finishRecording();
            });
            const auto messageTitle = videoPlatform->isRecordingAudio() ? i18nc("recording notification title", "Ectolens is Recording with Sound")
                                                                        : i18nc("recording notification title", "Ectolens is Recording");
            auto getSimpleDefaultShortcut = [] {
                const auto shortcuts = KGlobalAccel::self()->shortcut(ShortcutActions::self()->recordRegionAction());
                if (shortcuts.contains(QKeySequence{Qt::META | Qt::Key_R})) {
                    return QKeySequence{Qt::META | Qt::Key_R};
                }
                return QKeySequence{};
            };
            auto getShortcut = [](const auto &list) {
                auto it = std::find_if(list.cbegin(), list.cend(), [](const QKeySequence &shortcut) {
                    return !shortcut.isEmpty();
                });
                return it != list.cend() ? *it : QKeySequence{};
            };
            QKeySequence stopShortcut = getSimpleDefaultShortcut();
            if (stopShortcut.isEmpty()) {
                auto mode = m_videoPlatform->recordingMode();
                if (mode == VideoPlatform::Screen) {
                    stopShortcut = getShortcut(KGlobalAccel::self()->shortcut(ShortcutActions::self()->recordScreenAction()));
                } else if (mode == VideoPlatform::Window) {
                    stopShortcut = getShortcut(KGlobalAccel::self()->shortcut(ShortcutActions::self()->recordWindowAction()));
                } else if (mode == VideoPlatform::Region) {
                    stopShortcut = getShortcut(KGlobalAccel::self()->shortcut(ShortcutActions::self()->recordRegionAction()));
                }
            }
            const auto messageBody = stopShortcut.isEmpty()
                ? i18nc("recording notification message without shortcut", "To finish the recording, click the pulsing red System Tray icon.")
                : xi18nc("recording notification message with shortcut", "To finish the recording, click the pulsing red System Tray icon or press <shortcut>%1</shortcut>.", stopShortcut.toString(QKeySequence::NativeText));
            auto notification = new KNotification(u"recordingStarted"_s, KNotification::CloseOnTimeout, this);
            notification->setTitle(messageTitle);
            notification->setText(messageBody);
            // Whether the notification is transient and should not be kept in history.
            notification->setHint(u"transient"_s, true);
            // Can't set notification duration with KNotification directly.
            // Also see https://bugs.kde.org/show_bug.cgi?id=503838
            QTimer::singleShot(4000, notification, &KNotification::close);
            connect(m_videoPlatform.get(), &VideoPlatform::recordingStateChanged, notification, [notification](VideoPlatform::RecordingState state) {
                if (state != VideoPlatform::RecordingState::Recording) {
                    notification->close();
                }
            });
            notification->sendEvent();
            if (!QMovie::supportedFormats().contains("webp"_ba)) {
                const auto messageTitle = i18nc("missing webp support notification title", "WebP support is missing.");
                const auto messageBody = i18nc("missing webp support notification message", "Please install Qt Image Formats to get animated system tray icons for Ectolens, and then report this packaging issue to your distributor.");
                s_systemTrayIcon->showMessage(messageTitle, messageBody, u"dialog-warning"_s, 4000);
                s_systemTrayIcon->setIconByName(u"media-record"_s);
                return;
            }
            static const auto initPulseAnimation = [] {
                auto animation = new QMovie(recordingPulseIcon, {}, s_systemTrayIcon.get());
                animation->setCacheMode(QMovie::CacheAll);
                connect(animation, &QMovie::frameChanged, animation, [animation] {
                    s_systemTrayIcon->setIconByPixmap(animation->currentPixmap());
                });
                // We periodically switch to a static image instead of having a period in the
                // animation where nothing happens to be a bit more efficient.
                // NOTE: If QMovie::finished isn't working,
                // you probably edited the icon and forgot to set the loop count.
                connect(animation, &QMovie::finished, animation, [animation] {
                    animation->stop();
                    static const auto recordingPixmap = QPixmap{recordingIcon};
                    s_systemTrayIcon->setIconByPixmap(recordingPixmap);
                    const auto animationDuration = animation->nextFrameDelay() * animation->frameCount() / (animation->speed() / 100.0);
                    QTimer::singleShot(animationDuration / 2, animation, &QMovie::start);
                });
                animation->start();
            };
            auto startedAnimation = new QMovie(recordingStartedIcon, {}, s_systemTrayIcon.get());
            startedAnimation->setCacheMode(QMovie::CacheAll);
            connect(startedAnimation, &QMovie::frameChanged, startedAnimation, [startedAnimation] {
                s_systemTrayIcon->setIconByPixmap(startedAnimation->currentPixmap());
            });
            connect(startedAnimation, &QMovie::finished, startedAnimation, [startedAnimation] {
                startedAnimation->stop();
                startedAnimation->deleteLater();
                initPulseAnimation();
            });
            startedAnimation->start();
        } else if (state == VideoPlatform::RecordingState::Rendering && s_systemTrayIcon) {
            const auto messageTitle = i18nc("recording notification title", "Ectolens is Finishing the Recording");
            const auto messageBody = i18nc("recording notification message", "Please wait");
            s_systemTrayIcon->setToolTipTitle(i18nc("@info:tooltip title for rendering tray icon", //
                                                    "Ectolens is Finishing the Recording"));
            auto subtitle = i18nc("@info:tooltip subtitle for rendering tray icon", //
                                  "Time recorded: %1\n" //
                                  "Click to stop rendering early (this will lose data)",
                                  recordedTime());
            auto notification = new KNotification(u"recordingSaveInProgress"_s, KNotification::CloseOnTimeout, this);
            notification->setTitle(messageTitle);
            notification->setText(messageBody);
            notification->setHint(u"transient"_s, true);
            connect(m_videoPlatform.get(), &VideoPlatform::recordingStateChanged, notification, [notification](VideoPlatform::RecordingState state) {
                if (state != VideoPlatform::RecordingState::Rendering) {
                    notification->close();
                }
            });
            notification->sendEvent();
            s_systemTrayIcon->setToolTipSubTitle(subtitle);
        } else {
            s_systemTrayIcon.reset();
            m_captureWindows.clear();
        }
    });
    connect(videoPlatform, &VideoPlatform::recordedTimeChanged, this, [this, videoPlatform] {
        Q_EMIT recordedTimeChanged();
        if (!s_systemTrayIcon || videoPlatform->recordingState() != VideoPlatform::RecordingState::Recording) {
            return;
        }
        auto subtitle = i18nc("@info:tooltip subtitle for recording tray icon", //
                              "Time recorded: %1\n" //
                              "Click to finish recording",
                              recordedTime());
        s_systemTrayIcon->setToolTipSubTitle(subtitle);
    });
    connect(videoPlatform, &VideoPlatform::recordingSaved, this, [this](const QUrl &fileUrl) {
        CaptureTools::instance()->revertResizedWindow();
        // Always try to save. Needed to move recordings out of temp dir.
        ExportManager::instance()->exportVideo(autoExportActions() | ExportManager::Save, fileUrl, videoOutputUrl());
        m_recordingEventLoopLocker.reset();
    });
    connect(videoPlatform, &VideoPlatform::recordingCanceled, this, [this] {
        CaptureTools::instance()->revertResizedWindow();
        m_recordingEventLoopLocker.reset();
        if (m_startMode != StartMode::Gui || !m_returnToViewer || isGuiNull()) {
            Q_EMIT allDone();
            return;
        }
        SpectacleWindow::setTitleForAll(SpectacleWindow::Previous);
        const auto windows = SpectacleWindow::instances();
        if (windows.empty()) {
            initViewerWindow(ViewerWindow::Viewer);
            return;
        }
        for (auto w : windows) {
            w->setVisible(true);
        }
    });
    connect(videoPlatform, &VideoPlatform::recordingFailed, this, [this, onScreenshotOrRecordingFailed](const QString &message){
        if (m_agentRecording) {
            m_agentRecording = false;
            auto stop = std::move(m_agentStopReply);
            m_agentStopReply = nullptr;
            if (stop) {
                stop({{u"status"_s, u"error"_s}, {u"message"_s, message}});
            }
        }
        CaptureTools::instance()->revertResizedWindow();
        m_recordingEventLoopLocker.reset();
        auto uiMessage = i18nc("@info", "An error occurred while attempting to record the screen.");
        onScreenshotOrRecordingFailed(message, uiMessage, &SpectacleCore::dbusRecordingFailed);
    });
    connect(videoPlatform, &VideoPlatform::regionRequested, this, [this] {
        initCaptureWindows(CaptureWindow::Video);
        SpectacleWindow::setTitleForAll(SpectacleWindow::Unsaved);
        SpectacleWindow::setVisibilityForAll(QWindow::FullScreen);
        // SelectionEditor::reset has to be delayed until after all the DPRs are correct on Wayland.
        connect(m_captureWindows.front().get(), &CaptureWindow::allExposed, //
                SelectionEditor::instance(), &SelectionEditor::reset, Qt::SingleShotConnection);
    });

    // set up the export manager
    auto exportManager = ExportManager::instance();
    auto onImageExported = [this](const ExportManager::Actions &actions, const QUrl &url) {
        if (actions & ExportManager::AnySave) {
            m_editExistingUrl = url;
            m_outputUrl.clear();
            CaptureTools::instance()->setSavedUrl(url);
        }
        runAfterCapture(actions, url);
        if (actions & ExportManager::UserAction && Settings::quitAfterSaveCopyExport()) {
            deleteWindows();
        }

        if (isGuiNull()) {
            if (m_cliOptions[CommandLineOptions::NoNotify]) {
                // if we notify, we Q_EMIT allDone only if the user either dismissed the notification or pressed
                // the "Open" button, otherwise the app closes before it can react to it.
                if (actions & ExportManager::CopyImage) {
                    // Allow some time for clipboard content to transfer if '--nonotify' is used, see Bug #411263
                    // TODO: Find better solution
                    QTimer::singleShot(250, this, &SpectacleCore::allDone);
                } else {
                    Q_EMIT allDone();
                }
            } else {
                doNotify(ScreenCapture::Screenshot, actions, url);
            }
            return;
        }

        auto viewerWindow = ViewerWindow::instance();
        if (!viewerWindow) {
            return;
        }

        // Ectolens: messages in the window's strips say it plainly (design proposal 2, gap 4).
        if (actions & ExportManager::AnySave) {
            SpectacleWindow::setTitleForAll(SpectacleWindow::Saved, url.fileName());
            const bool inScreenshots = url.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash)
                == Settings::imageSaveLocation().adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash);
            const QString folder = inScreenshots ? i18nc("@info the folder captures save to", "Screenshots")
                                                 : url.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash).toLocalFile();
            QString text;
            if (actions & ExportManager::CopyImage) {
                text = i18nc("@info %1 file name, %2 folder", "Copied to the clipboard and saved as <b>%1</b> in %2", url.fileName().toHtmlEscaped(), folder.toHtmlEscaped());
            } else if (actions & ExportManager::CopyPath) {
                text = i18nc("@info %1 file name, %2 folder", "Saved as <b>%1</b> in %2. Its path is on the clipboard", url.fileName().toHtmlEscaped(), folder.toHtmlEscaped());
            } else {
                text = i18nc("@info %1 file name, %2 folder", "Saved as <b>%1</b> in %2", url.fileName().toHtmlEscaped(), folder.toHtmlEscaped());
            }
            InlineMessageModel::instance()->push(InlineMessageModel::Saved, text, url);
        } else if (actions & ExportManager::CopyImage) {
            InlineMessageModel::instance()->push(InlineMessageModel::Copied, i18nc("@info", "Copied to the clipboard"));
        } else if (actions & ExportManager::SaveDeclined) {
            InlineMessageModel::instance()->push(InlineMessageModel::Copied, i18nc("@info", "Not saved"));
        }
    };
    connect(exportManager, &ExportManager::imageExported, this, onImageExported);
    auto onVideoExported = [this](const ExportManager::Actions &actions, const QUrl &url) {
        if (m_agentRecording) {
            // an agent's recording is written: its file for record_stop
            m_agentRecording = false;
            m_agentLastRecording = url.toLocalFile();
            auto stop = std::move(m_agentStopReply);
            m_agentStopReply = nullptr;
            if (stop) {
                stop({{u"status"_s, u"saved"_s}, {u"path"_s, m_agentLastRecording}});
                m_agentLastRecording.clear();
            }
        }
        setCurrentVideo(url);
        runAfterCapture(actions, url);

        if (actions & ExportManager::UserAction && Settings::quitAfterSaveCopyExport()) {
            deleteWindows();
        } else if (!ViewerWindow::instance()) {
            showViewerIfGuiMode();
        }

        if (isGuiNull()) {
            if (m_cliOptions[CommandLineOptions::NoNotify]) {
                Q_EMIT allDone();
            } else {
                doNotify(ScreenCapture::Recording, actions, url);
            }
            return;
        }

        auto viewerWindow = ViewerWindow::instance();
        if (!viewerWindow) {
            return;
        }

        if (actions & ExportManager::AnySave) {
            SpectacleWindow::setTitleForAll(SpectacleWindow::Saved, url.fileName());
            const bool inVideos = url.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash)
                == Settings::videoSaveLocation().adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash);
            const QString folder = inVideos ? i18nc("@info the folder recordings save to", "Screencasts")
                                            : url.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash).toLocalFile();
            const QString text = actions & ExportManager::CopyPath
                ? i18nc("@info %1 file name, %2 folder", "Saved as <b>%1</b> in %2. Its path is on the clipboard", url.fileName().toHtmlEscaped(), folder.toHtmlEscaped())
                : i18nc("@info %1 file name, %2 folder", "Saved as <b>%1</b> in %2", url.fileName().toHtmlEscaped(), folder.toHtmlEscaped());
            InlineMessageModel::instance()->push(InlineMessageModel::Saved, text, url);
        } else if (actions & ExportManager::CopyPath) {
            InlineMessageModel::instance()->push(InlineMessageModel::Copied, i18nc("@info", "Copied the recording's path"));
        }
    };
    connect(exportManager, &ExportManager::videoExported, this, onVideoExported);

    auto onQRCodeScanned = [](const QVariant &result) {
        auto viewerWindow = ViewerWindow::instance();
        if (!viewerWindow) {
            return;
        }
        // Thanks to: https://stackoverflow.com/questions/1500260/detect-urls-in-text-with-javascript
        using QRE = QRegularExpression;
        auto text = [](const QVariant &result) -> QString {
            if (result.typeId() == QMetaType::QString) {
                // Not using xi18nc because it doesn't work with the link replacement logic.
                // Also see https://invent.kde.org/graphics/spectacle/-/merge_requests/432#note_1111125
                auto text = i18nc("@info %1 what a QR code says", //
                                  "Found a QR code: <b>%1</b>",
                                  result.toString().toHtmlEscaped());
                return text;
            }
            return i18nc("@info", "Found QR code with binary content.");
        }(result);
        InlineMessageModel::instance()->push(InlineMessageModel::Scanned, text, result);
    };
    connect(exportManager, &ExportManager::qrCodeScanned, this, onQRCodeScanned);

    auto onOcrTextRecognized = [this](const QString &text, const QStringList &languageCodes, bool success) {
        if (!success) {
            InlineMessageModel::instance()->push(InlineMessageModel::Error, i18nc("@info", "Text extraction failed"));
            return;
        }

        if (text.isEmpty()) {
            InlineMessageModel::instance()->push(InlineMessageModel::Copied, i18nc("@info", "No text found in the image"));
            return;
        }

        // Ectolens: say how much was read, and keep the text for the strip's Show Text.
        const int lines = text.trimmed().count(u'\n') + 1;
        InlineMessageModel::instance()->push(InlineMessageModel::Extracted,
                                             i18ncp("@info", "Copied the text: %1 line", "Copied the text: %1 lines", lines), text);

        // ensure program stays alive until the notification finishes.
        if (!m_eventLoopLocker) {
            m_eventLoopLocker = std::make_unique<QEventLoopLocker>();
        }

        auto notification = new KNotification(u"ocrTextExtracted"_s, KNotification::CloseOnTimeout, nullptr);
        notification->setTitle(i18nc("@info:notification title", "Text Extracted"));

        notifications.append(notification);

        auto ocrManager = OcrManager::instance();
        auto languageNames = ocrManager->availableLanguagesWithNames();

        QStringList displayLanguages;
        for (const QString &code : languageCodes) {
            QString displayName = languageNames.value(code, code);
            if (!displayLanguages.contains(displayName)) {
                displayLanguages.append(displayName);
            }
        }

        QString languagesText;
        if (displayLanguages.size() == 1) {
            languagesText = displayLanguages.first();
        } else if (displayLanguages.size() == 2) {
            languagesText = i18nc("@info The variables are language names, e.g. 'English and German'", "%1 and %2", displayLanguages.at(0), displayLanguages.at(1));
        } else {
            languagesText = displayLanguages.join(u", "_s);
        }

        auto notificationText = xi18nc("@info:notification", "Text copied to clipboard.<nl/>Languages used: %1", languagesText);
        notification->setText(notificationText);
        notification->setIconName(u"document-scan"_s);

        if (!text.isEmpty()) {
            auto openEditorAction = notification->addAction(i18nc("@action:button", "Open in Text Editor"));
            connect(openEditorAction, &KNotificationAction::activated, this, [text, notification]() {
                // Create temporary file with extracted text
                auto exportManager = ExportManager::instance();
                exportManager->updateTimestamp();
                auto timestamp = exportManager->timestamp();

                QString filename = QStringLiteral("spectacle_ocr_%1.txt").arg(timestamp.toString(QStringLiteral("yyyyMMdd_HHmmss")));
                QString templatePath = QDir::tempPath() + QStringLiteral("/") + filename;

                QTemporaryFile tempFile;
                tempFile.setFileTemplate(templatePath);
                tempFile.setAutoRemove(false);

                if (tempFile.open()) {
                    QTextStream stream(&tempFile);
                    stream << text;
                    tempFile.close();

                    auto job = new KIO::OpenUrlJob(QUrl::fromLocalFile(tempFile.fileName()));
                    job->setStartupId(notification->xdgActivationToken().toUtf8());
                    job->setDeleteTemporaryFile(true);
                    job->start();
                }

                notification->close();
            });
        }

        auto onExpired = [this, notification] {
            notifications.removeOne(static_cast<KNotification *>(notification));

            if (notifications.empty() && m_eventLoopLocker) {
                QTimer::singleShot(250, this, [this] {
                    m_eventLoopLocker.reset();
                });
            }
        };
        connect(notification, &QObject::destroyed, this, onExpired);
        // We keep the application running for a while to ensure the notification action remains available in the history.
        // See: https://bugs.kde.org/show_bug.cgi?id=514434
        QTimer::singleShot(180000, notification, onExpired);

        notification->sendEvent();

        if (m_quitAfterOcr) {
            m_quitAfterOcr = false;
            deleteWindows();
        }
    };

    // Connect to OCR manager
    connect(OcrManager::instance(), &OcrManager::textRecognized, this, onOcrTextRecognized);
    connect(OcrManager::instance(), &OcrManager::statusChanged, this, [this](OcrManager::OcrStatus) {
        Q_EMIT ocrStatusChanged();
    });

    connect(exportManager, &ExportManager::errorMessage, this, &SpectacleCore::showErrorMessage);

    connect(m_annotationDocument.get(), &AnnotationDocument::repaintNeeded, m_annotationSyncTimer.get(), qOverload<>(&QTimer::start));
    connect(m_annotationSyncTimer.get(), &QTimer::timeout, this, [this] {
        auto image = m_annotationDocument->renderToImage();
        auto canvasPos = m_annotationDocument->canvasRect().topLeft();
        ImageMetaData::setLogicalXY(image, canvasPos.x(), canvasPos.y());
        ExportManager::instance()->setImage(image);
    }, Qt::QueuedConnection); // QueuedConnection to help prevent making the visible render lag.

    // set up shortcuts
    KGlobalAccel::self()->setGlobalShortcut(ShortcutActions::self()->openAction(),
                                            QList<QKeySequence>{
                                                Qt::Key_Print,
                                                // Default screenshot shortcut on Windows.
                                                // Also for keyboards without a print screen key.
                                                Qt::META | Qt::SHIFT | Qt::Key_S,
                                            });
    KGlobalAccel::self()->setGlobalShortcut(ShortcutActions::self()->fullScreenAction(), Qt::SHIFT | Qt::Key_Print);
    KGlobalAccel::self()->setGlobalShortcut(ShortcutActions::self()->activeWindowAction(), Qt::META | Qt::Key_Print);
    KGlobalAccel::self()->setGlobalShortcut(ShortcutActions::self()->windowUnderCursorAction(), Qt::META | Qt::CTRL | Qt::Key_Print);
    KGlobalAccel::self()->setGlobalShortcut(ShortcutActions::self()->regionAction(), Qt::META | Qt::SHIFT | Qt::Key_Print);
    KGlobalAccel::self()->setGlobalShortcut(ShortcutActions::self()->currentScreenAction(), QList<QKeySequence>());
    KGlobalAccel::self()->setGlobalShortcut(ShortcutActions::self()->openWithoutScreenshotAction(), QList<QKeySequence>());
    KGlobalAccel::self()->setGlobalShortcut(ShortcutActions::self()->recordScreenAction(), Qt::META | Qt::ALT | Qt::Key_R);
    KGlobalAccel::self()->setGlobalShortcut(ShortcutActions::self()->recordWindowAction(), Qt::META | Qt::CTRL | Qt::Key_R);
    KGlobalAccel::self()->setGlobalShortcut(ShortcutActions::self()->recordRegionAction(),
                                            QList<QKeySequence>{
                                                // Similar to region screenshot
                                                Qt::META | Qt::SHIFT | Qt::Key_R,
                                                // Also use Meta+R for now
                                                Qt::META | Qt::Key_R,
                                            });

    connect(qApp, &QApplication::screenRemoved, this, [this](QScreen *screen) {
        // It's dangerous to erase from within a for loop, so we use std::find_if
        auto hasScreen = [screen](const CaptureWindow::UniquePointer &window) {
            return window->screen() == screen;
        };
        auto it = std::find_if(m_captureWindows.begin(), m_captureWindows.end(), hasScreen);
        if (it != m_captureWindows.end()) {
            m_captureWindows.erase(it);
        }
    });
}

bool SpectacleCore::ocrAvailable() const
{
    return OcrManager::instance()->isAvailable();
}

OcrManager::OcrStatus SpectacleCore::ocrStatus() const
{
    return OcrManager::instance()->status();
}

QVariantMap SpectacleCore::ocrAvailableLanguages() const
{
    auto ocrManager = OcrManager::instance();
    if (!ocrManager->isAvailable()) {
        return QVariantMap();
    }

    auto languageMap = ocrManager->availableLanguagesWithNames();
    QVariantMap result;
    for (auto it = languageMap.constBegin(); it != languageMap.constEnd(); ++it) {
        result[it.key()] = it.value();
    }
    return result;
}

bool SpectacleCore::startOcrExtraction(const QString &languageCode)
{
    if (m_videoMode) {
        return false;
    }

    const bool hasCaptureWindows = !CaptureWindow::instances().isEmpty();

    if (hasCaptureWindows) {
        auto selectionEditor = SelectionEditor::instance();
        auto inlineMessages = InlineMessageModel::instance();

        m_ocrExportInProgress = true;
        if (!selectionEditor->acceptSelection(ExportManager::UserAction)) {
            m_ocrExportInProgress = false;
            inlineMessages->push(InlineMessageModel::Error, i18nc("@info", "Please select a region before extracting text"));
            return false;
        }

        QMetaObject::invokeMethod(
            this,
            [this, languageCode]() {
                performOcrExtraction(languageCode);
            },
            Qt::QueuedConnection);
        return true;
    }

    return performOcrExtraction(languageCode);
}

bool SpectacleCore::performOcrExtraction(const QString &languageCode)
{
    auto ocrManager = OcrManager::instance();
    auto inlineMessages = InlineMessageModel::instance();

    if (!ocrManager->isAvailable()) {
        inlineMessages->push(InlineMessageModel::Error, i18nc("@info", "OCR is not available."));
        return false;
    }

    const QImage image = m_annotationDocument->renderToImage();
    if (image.isNull()) {
        inlineMessages->push(InlineMessageModel::Error, i18nc("@info", "No screenshot available."));
        return false;
    }

    inlineMessages->push(InlineMessageModel::Copied, i18nc("@info", "Extracting text from image..."));

    if (languageCode.isEmpty()) {
        ocrManager->recognizeText(image);
    } else {
        ocrManager->recognizeTextWithLanguage(image, languageCode);
    }

    return true;
}

SpectacleCore::~SpectacleCore() noexcept
{
    s_self = nullptr;
}

SpectacleCore *SpectacleCore::instance()
{
    if (!s_self) {
        s_self = new SpectacleCore(qApp);
    }

    return s_self;
}

ImagePlatform *SpectacleCore::imagePlatform() const
{
    return m_imagePlatform.get();
}

VideoPlatform *SpectacleCore::videoPlatform() const
{
    return m_videoPlatform.get();
}

AnnotationDocument *SpectacleCore::annotationDocument() const
{
    return m_annotationDocument.get();
}

QUrl SpectacleCore::screenCaptureUrl() const
{
    return m_screenCaptureUrl;
}

void SpectacleCore::setScreenCaptureUrl(const QUrl &url)
{
    if(m_screenCaptureUrl == url) {
        return;
    }
    m_screenCaptureUrl = url;
    Q_EMIT screenCaptureUrlChanged();
}

void SpectacleCore::setScreenCaptureUrl(const QString &filePath)
{
    if (QDir::isRelativePath(filePath)) {
        setScreenCaptureUrl(QUrl::fromUserInput(QDir::current().absoluteFilePath(filePath)));
    } else {
        setScreenCaptureUrl(QUrl::fromUserInput(filePath));
    }
}

QUrl SpectacleCore::outputUrl() const
{
    return m_outputUrl.isEmpty() ? m_editExistingUrl : m_outputUrl;
}

int SpectacleCore::captureTimeRemaining() const
{
    int totalDuration = m_delayAnimation->totalDuration();
    int currentTime = m_delayAnimation->currentTime();
    return currentTime > totalDuration || m_delayAnimation->state() == QVariantAnimation::Stopped ?
        0 : totalDuration - currentTime;
}

qreal SpectacleCore::captureProgress() const
{
    // using currentValue() sometimes gives 1.0 when we don't want it.
    return m_delayAnimation->state() == QVariantAnimation::Stopped ?
        0 : m_delayAnimation->currentValue().toReal();
}

void SpectacleCore::activate(const QStringList &arguments, const QString &workingDirectory)
{
    if (m_videoPlatform->isRecording()) {
        // BUG: https://bugs.kde.org/show_bug.cgi?id=481471
        // TODO: find a way to support screenshot shortcuts while recording?
        finishRecording();
        return;
    }
    if (closeOverlayOnSecondPress()) {
        return;
    }
    if (!workingDirectory.isEmpty()) {
        QDir::setCurrent(workingDirectory);
    }

    // We can't re-use QCommandLineParser instances, it preserves earlier parsed values
    QCommandLineParser parser;
    parser.addOptions(CommandLineOptions::self()->allOptions);
    parser.parse(arguments);

    // Collect parsed command line options
    using Option = CommandLineOptions::Option;
    m_cliOptions.fill(false); // reset all values to false
    int optionsToCheck = parser.optionNames().size();
    for (int i = 0; optionsToCheck > 0 && i < CommandLineOptions::self()->allOptions.size(); ++i) {
        m_cliOptions[i] = parser.isSet(CommandLineOptions::self()->allOptions[i]);
        if (m_cliOptions[i]) {
            --optionsToCheck;
        }
    }

    // Determine start mode
    m_startMode = StartMode::Gui; // Default to Gui
    // Gui is an option that's normally useless since it's the default mode.
    // Make it override the other modes if explicitly set, using the launchonly option,
    // or editing an existing image. Editing an existing image requires a viewer window.
    if (!m_cliOptions[Option::Gui]
        && !m_cliOptions[Option::LaunchOnly]
        && !m_cliOptions[Option::EditExisting]) {
        // Background gets precedence over DBus
        if (m_cliOptions[Option::Background]) {
            m_startMode = StartMode::Background;
        } else if (m_cliOptions[Option::DBus]) {
            m_startMode = StartMode::DBus;
        }
    }

    if (parser.optionNames().size() > 0 || m_startMode != StartMode::Gui || !m_returnToViewer) {
        // Delete windows if we have CLI options or not in GUI mode.
        // We don't want to delete them otherwise because that will mess with the
        // settings for PrintScreen key behavior.
        deleteWindows();
    }

    // reset last region if it should not be remembered across restarts
    if (!(Settings::rememberSelectionRect() == Settings::EnumRememberSelectionRect::Always)) {
        Settings::setSelectionRect({0, 0, 0, 0});
    }

    /* The logic for setting options for each start mode:
     *
     * - Gui/DBus: Prioritise command line options and default to saved settings.
     * - Background: Prioritise command line options and use defaults based on
     *   how command line options are meant to be used.
     *
     * Never start with a delay by default. It is annoying and confuses users
     * when nothing happens immediately after starting spectacle.
     */

    bool onClick;
    bool includeDecorations;
    bool includePointer;
    bool includeShadow;
    if (m_startMode == StartMode::Background) {
        onClick = m_cliOptions[Option::OnClick];
        includeDecorations = !m_cliOptions[Option::NoDecoration];
        includePointer = m_cliOptions[Option::Pointer];
        includeShadow = !m_cliOptions[Option::NoShadow];
    } else {
        onClick = Settings::captureOnClick() || m_cliOptions[Option::OnClick];
        includeDecorations = Settings::includeDecorations()
                            && !m_cliOptions[Option::NoDecoration];
        includeShadow = Settings::includeShadow() && !m_cliOptions[Option::NoShadow];
        includePointer = Settings::includePointer() || m_cliOptions[Option::Pointer];
    }
    onClick &= m_imagePlatform->supportedShutterModes().testFlag(ImagePlatform::OnClick);

    int delayMsec = 0; // default to 0 if cli value parse fails
    if (onClick) {
        delayMsec = -1;
    } else if (m_cliOptions[Option::Delay]) {
        bool parseOk = false;
        int value = parser.value(CommandLineOptions::self()->delay).toInt(&parseOk);
        if (parseOk) {
            delayMsec = value;
        }
    }

    if (m_cliOptions[Option::EditExisting]) {
        auto input = parser.value(CommandLineOptions::self()->editExisting);
        m_editExistingUrl = QUrl::fromUserInput(input, QDir::currentPath(), QUrl::AssumeLocalFile);
        // QFileInfo::exists() only works with local files.
        auto existingLocalFile = m_editExistingUrl.toLocalFile();
        if (QFileInfo::exists(existingLocalFile)) {
            InlineMessageModel::instance()->clear();
            // If editing an existing image, open the annotation editor.
            // This QImage constructor only works with local files or Qt resource file names.
            QImage existingImage(existingLocalFile);
            m_annotationDocument->clearAnnotations();
            m_annotationDocument->setBaseImage(existingImage);
            m_returnToViewer = true;
            showViewerIfGuiMode();
            SpectacleWindow::setTitleForAll(SpectacleWindow::Saved, m_editExistingUrl.fileName());
            if (!existingImage.isNull()) {
                setExportImage(existingImage);
                ExportManager::instance()->scanQRCode();
            }
            return;
        } else {
            m_cliOptions[Option::EditExisting] = false;
            m_editExistingUrl.clear();
        }
    } else {
        m_editExistingUrl.clear();
    }

    if (m_cliOptions[Option::Output]) {
        m_outputUrl = QUrl::fromUserInput(parser.value(CommandLineOptions::self()->output),
                                          QDir::currentPath(), QUrl::AssumeLocalFile);
        if (!m_outputUrl.isValid()) {
            m_cliOptions[Option::Output] = false;
            m_outputUrl.clear();
        }
    } else {
        m_outputUrl.clear();
    }

    // Determine grab mode
    using CaptureMode = CaptureModeModel::CaptureMode;
    using GrabMode = ImagePlatform::GrabMode;
    auto cliGrabMode = [&]() -> std::optional<GrabMode> {
        if (m_cliOptions[Option::Fullscreen]) {
            return m_cliOptions[Option::Scaled] ? GrabMode::AllScreensScaled : GrabMode::AllScreens;
        } else if (m_cliOptions[Option::Current]) {
            return GrabMode::CurrentScreen;
        } else if (m_cliOptions[Option::ActiveWindow]) {
            return GrabMode::ActiveWindow;
        } else if (m_cliOptions[Option::Region]) {
            return GrabMode::PerScreenImageNative;
        } else if (m_cliOptions[Option::WindowUnderCursor]) {
            return toGrabMode(CaptureMode::WindowUnderCursor);
        }
        return std::nullopt;
    };
    auto launchActionGrabMode = [&] {
        switch (Settings::launchAction()) {
        case Settings::TakeRectangularScreenshot:
            return GrabMode::PerScreenImageNative;
        case Settings::TakeFullscreenScreenshot:
            return GrabMode::AllScreens;
        case Settings::UseLastUsedCapturemode:
            return toGrabMode(CaptureMode(Settings::captureMode()));
        default:
            return GrabMode::NoGrabModes;
        }
    };
    auto grabMode = cliGrabMode().value_or(m_startMode == StartMode::Background ? GrabMode::AllScreens : launchActionGrabMode());

    if (m_cliOptions[Option::Scaled] && !m_cliOptions[Option::Fullscreen]) {
        qWarning().noquote() << i18nc("@info:shell", "--scaled is only allowed together with -f/--fullscreen and will be ignored");
    }

    using RecordingMode = VideoPlatform::RecordingMode;
    RecordingMode recordingMode = RecordingMode::NoRecordingModes;
    if (m_cliOptions[Option::Record]) {
        auto input = parser.value(CommandLineOptions::self()->record);
        if (input.startsWith(u"s"_s, Qt::CaseInsensitive)) {
            recordingMode = RecordingMode::Screen;
        } else if (input.startsWith(u"w"_s, Qt::CaseInsensitive)) {
            recordingMode = RecordingMode::Window;
        } else if (input.startsWith(u"r"_s, Qt::CaseInsensitive)) {
            recordingMode = RecordingMode::Region;
        } else {
            // QCommandLineParser handles the case where input is empty
            qWarning().noquote() << i18nc("@info:shell", "%1 is not a valid mode for --record", input);
            Q_EMIT allDone();
            return;
        }
        setVideoMode(true);

        if (m_startMode != StartMode::Background) {
            includePointer = Settings::videoIncludePointer() || m_cliOptions[Option::Pointer];
        }
    } else {
        setVideoMode(false);
    }

    if (m_cliOptions[Option::ReleaseCapture]) {
        SelectionEditor::instance()->forceReleaseCapture(true);
    }

    // If any capture mode is given in the cli options, let it override
    // the setting to not take a screenshot on launch
    // clang-format off
    bool captureModeFromCli =
        m_cliOptions[Option::Fullscreen] ||
        m_cliOptions[Option::Current] ||
        m_cliOptions[Option::ActiveWindow] ||
        m_cliOptions[Option::WindowUnderCursor] ||
        m_cliOptions[Option::Region] ||
        m_cliOptions[Option::Record];
    // clang-format on

    switch (m_startMode) {
    case StartMode::DBus:
        break;
    case StartMode::Background:
        if (m_videoMode) {
            startRecording(recordingMode, includePointer);
        } else {
            takeNewScreenshot(grabMode, delayMsec, includePointer, includeDecorations, includeShadow);
        }
        break;
    case StartMode::Gui:
        if (isGuiNull()) {
            if (m_cliOptions[Option::LaunchOnly] || //
                (Settings::launchAction() == Settings::DoNotTakeScreenshot && !captureModeFromCli)) {
                initViewerWindow(ViewerWindow::Dialog);
                ViewerWindow::instance()->setVisible(true);
            } else {
                if (m_videoMode) {
                    startRecording(recordingMode, includePointer);
                } else {
                    takeNewScreenshot(grabMode, delayMsec, includePointer, includeDecorations, includeShadow);
                }
            }
        } else {
            using Actions = Settings::EnumPrintKeyRunningAction;
            switch (Settings::printKeyRunningAction()) {
            case Actions::TakeNewScreenshot: {
                // takeNewScreenshot switches to on click if immediate is not supported.
                takeNewScreenshot(grabMode, 0, includePointer, includeDecorations, includeShadow);
                break;
            }
            case Actions::FocusWindow: {
                bool isCaptureWindow = !CaptureWindow::instances().isEmpty();
                SpectacleWindow *window = nullptr;
                if (isCaptureWindow) {
                    window = CaptureWindow::instances().constFirst();
                } else {
                    window = ViewerWindow::instance();
                }
                if (isCaptureWindow) {
                    SpectacleWindow::setVisibilityForAll(QWindow::FullScreen);
                } else {
                    // Unminimize the window.
                    window->unminimize();
                }
                window->requestActivate();
                break;
            }
            case Actions::StartNewInstance: {
                QProcess newInstance;
                newInstance.setProgram(QCoreApplication::applicationFilePath());
                newInstance.setArguments({
                    CommandLineOptions::toArgument(CommandLineOptions::self()->newInstance)
                });
                newInstance.startDetached();
                break;
            }
            }
        }

        break;
    }
}

void SpectacleCore::takeNewScreenshot(ImagePlatform::GrabMode grabMode, int timeout, bool includePointer, bool includeDecorations, bool includeWindowShadow)
{
    // Clear when a new screenshot is taken to avoid overwriting
    // the previous file with a completely unrelated image.
    m_editExistingUrl.clear();
    m_cliOptions[CommandLineOptions::EditExisting] = false;

    m_delayAnimation->stop();

    m_captureFromWindow = ViewerWindow::instance() != nullptr;
    m_afterCapture = {};
    m_retaking = false;
    m_lastGrabMode = grabMode;
    m_lastIncludePointer = includePointer;
    m_lastIncludeDecorations = includeDecorations;
    m_lastIncludeShadow = includeWindowShadow;

    if ((timeout < 0 || !m_imagePlatform->supportedShutterModes().testFlag(ImagePlatform::Immediate))
        && m_imagePlatform->supportedShutterModes().testFlag(ImagePlatform::OnClick)
    ) {
        SpectacleWindow::setVisibilityForAll(QWindow::Hidden);
        m_imagePlatform->doGrab(ImagePlatform::ShutterMode::OnClick, m_lastGrabMode, m_lastIncludePointer, m_lastIncludeDecorations, m_lastIncludeShadow);
        return;
    }

    timeout = qMax(0, timeout);
    const bool noDelay = timeout == 0;
#if WITH_X11
    if (PlasmaVersion::get() < PlasmaVersion::check(5, 27, 4) && KX11Extras::compositingActive()) {
        // when compositing is enabled, we need to give it enough time for the window
        // to disappear and all the effects are complete before we take the shot. there's
        // no way of knowing how long the disappearing effects take, but as per default
        // settings (and unless the user has set an extremely slow effect), 200
        // milliseconds is a good amount of wait time.
        timeout = qMax(timeout, 200);
    } else if (qobject_cast<ImagePlatformXcb *>(m_imagePlatform.get())) {
        // X11 compositors (which may or may not be kwin) require small delay for
        // window to disappear.
        // Also, minimum 50ms delay is needed to prevent segfaults from xcb function
        // calls that don't get replies fast enough.
        timeout = qMax(timeout, 50);
    }
#endif

    if (noDelay) {
        SpectacleWindow::setVisibilityForAll(QWindow::Hidden);
        QTimer::singleShot(timeout, this, [this]() {
            m_imagePlatform->doGrab(ImagePlatform::ShutterMode::Immediate, m_lastGrabMode, m_lastIncludePointer, m_lastIncludeDecorations, m_lastIncludeShadow);
        });
        return;
    }

    m_delayAnimation->setDuration(timeout);
    m_delayAnimation->start();

    // skip minimize animation.
    SpectacleWindow::setVisibilityForAll(QWindow::Hidden);
    SpectacleWindow::setVisibilityForAll(QWindow::Minimized);
}

bool SpectacleCore::closeOverlayOnSecondPress()
{
    // Ectolens: Print Screen, or any capture shortcut, pressed while the
    // overlay is up closes it, as Esc does: the key that opens a capture also closes it. While
    // recording it stops and saves, as before. (Spectacle took a new screenshot instead.)
    if (CaptureWindow::instances().isEmpty() || m_videoPlatform->isRecording()) {
        return false;
    }
    TestScript::report(u"ECTOLENS-CLOSED-BY-SECOND-PRESS"_s);
    cancelScreenshot();
    return true;
}

// ── Ectolens: AI agents (AgentService, ectolens-mcp) ──

void SpectacleCore::agentCapture(const AgentRequest &request, AgentReply reply)
{
    auto fail = [&reply](const QString &status, const QString &message = {}) {
        QJsonObject result{{u"status"_s, status}};
        if (!message.isEmpty()) {
            result[u"message"_s] = message;
        }
        reply(result);
    };
    if (!Settings::agentAccess()) {
        fail(u"off"_s);
        return;
    }
    if (m_agent || m_agentRecording || m_videoPlatform->isRecording() || !CaptureWindow::instances().isEmpty() || ViewerWindow::instance()) {
        fail(u"busy"_s, i18nc("@info", "Ectolens is already open for a capture, a recording or the Ectolens window."));
        return;
    }
    if (request.target == u"window"_s && !WindowPicker::instance()->window(request.windowId)) {
        fail(u"error"_s, i18nc("@info", "No window has that id; list_windows gives the ids."));
        return;
    }
    if (request.target == u"area"_s && request.area.isEmpty()) {
        fail(u"error"_s, i18nc("@info", "The area needs a width and a height."));
        return;
    }
    if (m_agentIdle) {
        m_agentIdle->stop();
    }
    m_agent = request;
    m_agentAdjusting = false;
    m_agentSendWhenRead = false;
    // each request starts as Settings › Model Context Protocol (MCP) says; the dialog's box
    // changes this one only
    m_agentBlur = Settings::agentHidePrivateInfo();
    m_agentReply = std::move(reply);
    Q_EMIT agentChanged();
    TestScript::report(u"ECTOLENS-AGENT asking %1 %2"_s.arg(request.kind, request.target));
    takeNewScreenshot(CaptureModeModel::RectangularRegion, 0, false, false);
}

void SpectacleCore::frameForAgent()
{
    if (!m_agent) {
        return;
    }
    auto editor = SelectionEditor::instance();
    const auto &r = *m_agent;
    QString uuid;
    if (r.target == u"window"_s) {
        uuid = r.windowId;
    } else if (r.target == u"active_window"_s) {
        for (const auto &w : WindowPicker::instance()->visibleWindows()) {
            if (w.active) {
                uuid = w.uuid;
                break;
            }
        }
    }
    if (!uuid.isEmpty() && editor->pickWindow(uuid)) {
        // framed on the window, at its place now
    } else if (r.target == u"area"_s) {
        editor->selection()->setRect(r.area);
    } else {
        // the whole screen the person is looking at (the pointer's)
        QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
        if (!screen) {
            screen = QGuiApplication::primaryScreen();
        }
        editor->selection()->setRect(Geometry::mapFromPlatformRect(screen->geometry(), screen->devicePixelRatio()));
    }
    if (m_agentBlur && r.kind == u"capture"_s) {
        // blurred before the person decides, so they see what the agent would get. Not for a
        // recording: it is made from the live screen, which the overlay's blurs never reach, and
        // its dialog says so.
        CaptureTools::instance()->hidePrivateInfo(editor->selection()->rectF());
    }
    TestScript::report(u"ECTOLENS-AGENT framed"_s);
}

void SpectacleCore::sendToAgent(const QRectF &rect)
{
    const AgentRequest request = *m_agent;
    const bool blur = m_agentBlur;
    CaptureTools::instance()->revertResizedWindow();
    SpectacleWindow::setVisibilityForAll(QWindow::Hidden);
    deleteWindows();
    m_annotationDocument->cropCanvas(rect);
    syncExportImage();
    const QImage image = ExportManager::instance()->image();
    // a folder only this user can read, emptied when the session ends (the runtime folder)
    QDir dir(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation));
    dir.mkpath(u"ectolens-agent"_s);
    dir.cd(u"ectolens-agent"_s);
    QFile::setPermissions(dir.absolutePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    const QString path = dir.filePath(u"capture-%1.png"_s.arg(QDateTime::currentDateTime().toString(u"yyyyMMdd-HHmmss-zzz"_s)));
    QJsonObject result;
    if (image.isNull() || !image.save(path, "PNG")) {
        result = {{u"status"_s, u"error"_s}, {u"message"_s, i18nc("@info", "The picture could not be saved for the agent.")}};
    } else {
        result = {{u"status"_s, u"sent"_s}, {u"path"_s, path}, {u"width"_s, image.width()}, {u"height"_s, image.height()}};
        if (request.wantText) {
            result[u"text"_s] = CaptureTools::readText(image, blur);
        }
    }
    m_agent.reset();
    answerAgent(result);
    Q_EMIT agentChanged();
    TestScript::report(u"ECTOLENS-AGENT sent %1"_s.arg(path));
    agentIdle();
}

void SpectacleCore::agentIdle()
{
    if (m_startMode == StartMode::Gui) {
        return;
    }
    if (!m_agentIdle) {
        m_agentIdle = new QTimer(this);
        m_agentIdle->setSingleShot(true);
        m_agentIdle->setInterval(2 * 60 * 1000);
        connect(m_agentIdle, &QTimer::timeout, this, [this] {
            if (!m_agent && !m_agentRecording && !m_videoPlatform->isRecording() && CaptureWindow::instances().isEmpty() && !ViewerWindow::instance()) {
                Q_EMIT allDone();
            }
        });
    }
    m_agentIdle->start();
}

void SpectacleCore::agentRecordStop(AgentReply reply)
{
    if (m_agentRecording && m_videoPlatform->isRecording()) {
        m_agentStopReply = std::move(reply);
        finishRecording();
    } else if (!m_agentLastRecording.isEmpty()) {
        // the person stopped it already
        reply({{u"status"_s, u"saved"_s}, {u"path"_s, m_agentLastRecording}});
        m_agentLastRecording.clear();
    } else {
        reply({{u"status"_s, u"not_recording"_s}});
    }
}

void SpectacleCore::answerAgent(const QJsonObject &result)
{
    auto reply = std::move(m_agentReply);
    m_agentReply = nullptr;
    if (reply) {
        reply(result);
    }
}

bool SpectacleCore::agentAsking() const
{
    return m_agent.has_value();
}

bool SpectacleCore::agentBlur() const
{
    return m_agentBlur;
}

void SpectacleCore::setAgentBlur(bool blur)
{
    // a recording can't be blurred: it is made from the live screen
    if (!m_agent || m_agent->kind != u"capture"_s || blur == m_agentBlur) {
        return;
    }
    m_agentBlur = blur;
    auto tools = CaptureTools::instance();
    if (blur) {
        tools->putBackPrivateInfoBlurs(SelectionEditor::instance()->selection()->rectF());
    } else {
        m_agentSendWhenRead = false;
        tools->takeOffPrivateInfoBlurs();
    }
    TestScript::report(u"ECTOLENS-AGENT blur %1"_s.arg(blur ? u"on"_s : u"off"_s));
    Q_EMIT agentChanged();
}

QString SpectacleCore::agentKind() const
{
    return m_agent ? m_agent->kind : QString();
}

QString SpectacleCore::agentClient() const
{
    return m_agent && !m_agent->client.isEmpty() ? m_agent->client : i18nc("@info an AI agent with no name", "An agent");
}

QString SpectacleCore::agentReason() const
{
    return m_agent ? m_agent->reason : QString();
}

void SpectacleCore::agentSend()
{
    if (!m_agent) {
        return;
    }
    if (m_agent->kind == u"record"_s) {
        recordSelection();
    } else {
        SelectionEditor::instance()->acceptSelection();
    }
}

void SpectacleCore::agentRefuse()
{
    cancelScreenshot();
}

void SpectacleCore::agentAdjust()
{
    if (m_agent && !m_agentAdjusting) {
        m_agentAdjusting = true;
        Q_EMIT agentChanged();
    }
}

bool SpectacleCore::agentAdjusting() const
{
    return m_agent && m_agentAdjusting;
}

void SpectacleCore::markNextCaptureAsApiCall()
{
    m_nextIsApi = true;
}

void SpectacleCore::takeNewScreenshot(int captureMode, int timeout, bool includePointer, bool includeDecorations, bool includeShadow)
{
    m_apiCapture = m_nextIsApi;
    m_nextIsApi = false;
    using CaptureMode = CaptureModeModel::CaptureMode;
    if (timeout < 0 && !m_imagePlatform->supportedShutterModes().testFlag(ImagePlatform::OnClick)) {
        timeout = Settings::captureDelay() * 1000;
    }
    takeNewScreenshot(toGrabMode(CaptureMode(captureMode)), timeout, includePointer, includeDecorations, includeShadow);
}

void SpectacleCore::cancelScreenshot()
{
    if (m_agent) {
        // Esc, Don't Send or Print Screen again: the agent gets nothing
        if (m_recordDelay) {
            m_recordDelay->stop();
        }
        m_agent.reset();
        answerAgent({{u"status"_s, u"declined"_s}});
        Q_EMIT agentChanged();
        CaptureTools::instance()->revertResizedWindow();
        SpectacleWindow::setVisibilityForAll(QWindow::Hidden);
        deleteWindows();
        agentIdle();
        return;
    }
    if (m_recordDelay) {
        m_recordDelay->stop();
    }
    m_retaking = false;
    m_afterCapture = {};
    CaptureTools::instance()->revertResizedWindow();
    m_shareAsked = false;
    m_shareOnce.clear();
    if (m_startMode != StartMode::Gui || !m_returnToViewer) {
        Q_EMIT allDone();
        return;
    }

    int currentTime = m_delayAnimation->currentTime();
    m_delayAnimation->stop();
    if (currentTime > 0) {
        SpectacleWindow::setTitleForAll(SpectacleWindow::Previous);
    } else if (!ViewerWindow::instance()) {
        initViewerWindow(ViewerWindow::Viewer);
        ViewerWindow::instance()->setVisible(true);
    } else if (ViewerWindow::instance()) {
        Q_EMIT allDone();
    }
}

void SpectacleCore::showErrorMessage(const QString &message)
{
    qWarning().noquote() << message;

    if (m_startMode == StartMode::Gui || m_startMode == StartMode::DBus) {
        KMessageBox::error(nullptr, message);
    }
}

void SpectacleCore::showViewerIfGuiMode(bool minimized)
{
    if (m_startMode != StartMode::Gui || !m_returnToViewer) {
        return;
    }
    initViewerWindow(ViewerWindow::Viewer);
    if (!m_videoMode && m_cliOptions[CommandLineOptions::EditExisting]) {
        ViewerWindow::instance()->setAnnotating(true);
    }
    if (minimized) {
        ViewerWindow::instance()->showMinimized();
    } else {
        ViewerWindow::instance()->setVisible(true);
    }
}

void SpectacleCore::doNotify(ScreenCapture type, const ExportManager::Actions &actions, const QUrl &saveUrl)
{
    if (m_cliOptions[CommandLineOptions::NoNotify]) {
        return;
    }

    // ensure program stays alive until the notification finishes.
    if (!m_eventLoopLocker) {
        m_eventLoopLocker = std::make_unique<QEventLoopLocker>();
    }

    KNotification *notification = nullptr;
    QString title;
    // Use nullptr as the notification parent to prevent them from disappearing from history.
    if (type == ScreenCapture::Screenshot) {
        notification = new KNotification(u"newScreenshotSaved"_s, KNotification::CloseOnTimeout, nullptr);
        title = CaptureModeModel::captureModeLabel(toCaptureMode(m_lastGrabMode));
    } else {
        notification = new KNotification(u"recordingSaved"_s, KNotification::CloseOnTimeout, nullptr);
        title = RecordingModeModel::recordingModeLabel(m_lastRecordingMode);
    }
    notification->setTitle(title);

    notifications.append(notification);

    // a speaking message is prettier than a URL, special case for copy image/location to clipboard and the default pictures location
    const QString &saveDirPath = saveUrl.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash).path();
    const QString &saveFileName = saveUrl.fileName();

    using Action = ExportManager::Action;
    const bool savedFile = actions & Action::AnySave && !saveFileName.isEmpty();
    if (type == ScreenCapture::Screenshot) {
        // Ectolens: titled with what was taken ("Konsole 799×508"), and saying where it went
        // in a sentence (design proposal 2, step 2).
        if (!m_captureTitle.isEmpty()) {
            notification->setTitle(m_captureTitle);
        }
        const bool inScreenshots = saveUrl.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash)
            == Settings::imageSaveLocation().adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash);
        const QString folder = inScreenshots ? i18nc("@info the folder captures save to", "Screenshots") : saveDirPath;
        if (savedFile && actions & Action::CopyPath) {
            notification->setText(i18nc("@info %1 file name, %2 folder", "Saved as %1 in %2. Its path is on the clipboard.", saveFileName, folder));
        } else if (savedFile && actions & Action::CopyImage) {
            notification->setText(i18nc("@info %1 file name, %2 folder", "Copied to the clipboard and saved as %1 in %2.", saveFileName, folder));
        } else if (savedFile) {
            notification->setText(i18nc("@info %1 file name, %2 folder", "Saved as %1 in %2.", saveFileName, folder));
        } else if (actions & Action::SaveDeclined && actions & Action::CopyImage) {
            notification->setText(i18nc("@info", "Not saved. Copied to the clipboard instead."));
        } else if (actions & Action::SaveDeclined) {
            notification->setText(i18nc("@info", "Not saved."));
        } else if (actions & Action::CopyImage) {
            notification->setText(i18nc("@info", "Copied to the clipboard."));
        } else {
            notification->setText(i18nc("@info", "Not copied or saved."));
        }
    } else if (type == ScreenCapture::Recording && savedFile) {
        if (!m_captureTitle.isEmpty()) {
            notification->setTitle(m_captureTitle);
        }
        if (actions & Action::CopyPath) {
            notification->setText(
                i18nc("%1 is the filename, %2 the path to the save location", "A recording was saved as '%1' to '%2' and the file path of the recording has been saved to your clipboard.", saveFileName, saveDirPath));
        } else if (saveDirPath == QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)) {
            notification->setText(i18nc("%1 is the filename", "A recording was saved as '%1' to your Videos folder.", saveFileName));
        } else {
            notification->setText(i18nc("%1 is the filename, %2 the path to the save location", "A recording was saved as '%1' to '%2'.", saveFileName, saveDirPath));
        }
    }

    if (type == ScreenCapture::Screenshot && !savedFile) {
        // Not saved: this notification keeps its own copy of the picture (a later capture
        // must not change what its Save writes), shows it from its own file, and keeps
        // Ectolens running while its buttons can still be pressed from the notification history.
        const QImage picture = ExportManager::instance()->image();
        auto file = new QTemporaryFile(QDir::tempPath() + u"/ectolens-capture-XXXXXX.png"_s, notification);
        if (file->open() && picture.save(file, "PNG")) {
            file->flush();
            notification->setUrls({QUrl::fromLocalFile(file->fileName())});
        }
        auto saveAction = notification->addAction(i18nc("@action:button save the capture to Screenshots", "Save"));
        connect(saveAction, &KNotificationAction::activated, this, [picture] {
            ExportManager::instance()->setImage(picture);
            ExportManager::instance()->exportImage(ExportManager::Save);
        });
        auto annotateAction = notification->addAction(i18n("Annotate"));
        connect(annotateAction, &KNotificationAction::activated, this, [this, picture] {
            m_annotationDocument->clearAnnotations();
            m_annotationDocument->setBaseImage(picture);
            setExportImage(picture);
            initViewerWindow(ViewerWindow::Viewer);
            ViewerWindow::instance()->setVisible(true);
            ViewerWindow::instance()->requestActivate();
        });
    } else if (!saveUrl.isEmpty()) {
        notification->setUrls({saveUrl});

        auto open = [notification, saveUrl]() {
            auto job = new KIO::OpenUrlJob(saveUrl);
            job->setStartupId(notification->xdgActivationToken().toUtf8());
            job->start();
        };
        auto defaultAction = notification->addDefaultAction(i18nc("Open the screenshot we just saved", "Open"));
        connect(defaultAction, &KNotificationAction::activated, this, open);
        auto openAction = notification->addAction(i18nc("@action:button open the saved capture", "Open"));
        connect(openAction, &KNotificationAction::activated, this, open);

        if (type == ScreenCapture::Screenshot) {
            auto folderAction = notification->addAction(i18nc("@action:button", "Show in Folder"));
            connect(folderAction, &KNotificationAction::activated, this, [saveUrl] {
                CaptureTools::instance()->showInFolder(saveUrl);
            });
            auto annotate = [saveUrl]() {
                QProcess newInstance;
                newInstance.setProgram(QCoreApplication::applicationFilePath());
                newInstance.setArguments({
                    CommandLineOptions::toArgument(CommandLineOptions::self()->newInstance),
                    CommandLineOptions::toArgument(CommandLineOptions::self()->editExisting),
                    saveUrl.toLocalFile()
                });
                newInstance.startDetached();
            };
            auto annotateAction = notification->addAction(i18n("Annotate"));
            connect(annotateAction, &KNotificationAction::activated, this, annotate);
        }
    }

    auto onExpired = [this, notification] {
        notifications.removeOne(static_cast<KNotification *>(notification));
        // When there are no more notifications running, we can remove the loop locker.
        if (notifications.empty() && m_eventLoopLocker) {
            QTimer::singleShot(250, this, [this] {
                m_eventLoopLocker.reset();
            });
        }
    };
    connect(notification, &QObject::destroyed, this, onExpired);
    // BUG: https://bugs.kde.org/show_bug.cgi?id=503838
    // We can't call KNotification::close or else the notifications will be removed from history.
    // 10 seconds is roughly the expected default duration. Ectolens: an unsaved capture's
    // Save and Annotate keep working from the history for three minutes, as Extract Text's do.
    QTimer::singleShot(type == ScreenCapture::Screenshot && !savedFile ? 180000 : 10000, notification, onExpired);

    notification->sendEvent();
}

ExportManager::Actions SpectacleCore::autoExportActions() const
{
    using Action = ExportManager::Action;
    using Option = CommandLineOptions::Option;
    bool save = (m_startMode != StartMode::Gui && m_cliOptions[Option::Output]) || m_videoMode;
    bool copyImage = m_cliOptions[Option::CopyImage] && !m_videoMode;
    bool copyPath = m_cliOptions[Option::CopyPath];
    bool saveAs = false;
    ExportManager::Actions actions;
    if (m_startMode != StartMode::Background) {
        // Ectolens: After Capture (Capture's arrow, Settings › General). Copy to clipboard
        // is on by default and Save to file off.
        save |= Settings::autoSaveImage();
        saveAs = Settings::autoSaveImage() && Settings::afterCaptureSaveWhere() == Settings::AskWhereEachTime && !m_videoMode;
        const bool copy = Settings::afterCaptureCopy();
        // A file path needs the saved file: should File Path be set without Save to file (the
        // menus switch it back to Image, but a config file can say anything), copy the image.
        const bool path = Settings::afterCaptureCopyWhat() == Settings::CopyTheFilePath && save;
        copyImage |= copy && !path && !m_videoMode;
        copyPath |= copy && path;
    }
    if (m_startMode == StartMode::Background || m_apiCapture) {
        // In background mode, and for D-Bus API calls (scripts expect a file, as Spectacle
        // promised), ensure that either save or copy image is enabled, as before.
        if (m_apiCapture) {
            saveAs = false;
            save |= !copyImage;
        }
        actions.setFlag(Action::Save, save || !copyImage);
        actions.setFlag(Action::CopyImage, !actions.testFlag(Action::Save) || copyImage);
    } else {
        actions.setFlag(Action::Save, save && !saveAs);
        actions.setFlag(Action::SaveAs, saveAs);
        actions.setFlag(Action::CopyImage, copyImage);
    }
    actions.setFlag(Action::CopyPath, (actions & Action::AnySave) && copyPath);
    return actions;
}

QString SpectacleCore::captureTitle(const QRectF &rect) const
{
    // The notification's title says what was taken: "Konsole 799×508" (design proposal 2).
    qreal dpr = 1;
    for (auto window : CaptureWindow::instances()) {
        if (QRectF(window->screenToFollow()->geometry()).intersects(rect)) {
            dpr = std::max(dpr, window->devicePixelRatio());
        }
    }
    const QSize px = (rect.size() * dpr).toSize();
    const QString size = i18nc("@info width×height", "%1×%2", QString::number(px.width()), QString::number(px.height()));
    const auto editor = SelectionEditor::instance();
    if (editor->frameIsWindow() && !editor->frameAppName().isEmpty()) {
        return i18nc("@title %1 app name, %2 size", "%1 %2", editor->frameAppName(), size);
    }
    for (auto screen : qApp->screens()) {
        if (QRectF(screen->geometry()) == rect) {
            return i18nc("@title %1 size", "Full screen %1", size);
        }
    }
    return i18nc("@title %1 size of an area you drew", "Area %1", size);
}

void SpectacleCore::runAfterCapture(const ExportManager::Actions &actions, const QUrl &url)
{
    if (!m_afterCapture.pending) {
        return;
    }
    const auto after = m_afterCapture;
    m_afterCapture = {};
    const bool saved = actions & ExportManager::AnySave && url.isValid();
    auto exportManager = ExportManager::instance();
    // Open with and Share get the saved file, or their own copy of the picture: the shared
    // temporary file is replaced by the next capture, maybe before the app or upload reads it.
    // The copy is removed after half an hour.
    QUrl copy;
    auto fileFor = [&]() {
        if (saved) {
            return url;
        }
        if (copy.isEmpty()) {
            QTemporaryFile file(QDir::tempPath() + u"/ectolens-capture-XXXXXX.png"_s);
            file.setAutoRemove(false);
            if (file.open() && exportManager->image().save(&file, "PNG")) {
                copy = QUrl::fromLocalFile(file.fileName());
                const QString path = file.fileName();
                QTimer::singleShot(30 * 60 * 1000, qApp, [path] {
                    QFile::remove(path);
                });
            }
        }
        return copy;
    };
    if (after.open && !after.openApp.isEmpty()) {
        CaptureTools::instance()->openWith(after.openApp, fileFor());
    }
    if (after.showInFolder && saved) {
        CaptureTools::instance()->showInFolder(url);
    }
    if (after.share && !after.shareTarget.isEmpty()) {
        CaptureTools::instance()->share(after.shareTarget, fileFor());
    }
}

void SpectacleCore::captureSharingWith(const QString &pluginId)
{
    m_shareAsked = true;
    m_shareOnce = pluginId;
    SelectionEditor::instance()->acceptSelection();
}

void SpectacleCore::closeOverlayQuietly()
{
    CaptureTools::instance()->revertResizedWindow();
    SpectacleWindow::setVisibilityForAll(QWindow::Hidden);
    m_captureWindows.clear();
    if (m_captureFromWindow) {
        // the capture started from the Ectolens window: it comes back
        initViewerWindow(ViewerWindow::Viewer);
        ViewerWindow::instance()->setVisible(true);
    }
}

void SpectacleCore::retakeFrozenPicture(const QString &reframeWindowUuid)
{
    // The overlay stays up the whole time: KWin
    // takes a picture of just the resized window, and it goes into the frozen picture where
    // the window is now. Twice, to speed it up: at once, as soon as
    // the app has drawn at its new size, so the picture follows the resize; and again a
    // second later, quietly, so a hint the app shows for a moment after a resize (Konsole
    // shows the terminal's size) doesn't stay in the picture.
    m_retakeUuid = reframeWindowUuid;
    m_retaking = true;
    const int generation = ++m_retakeGeneration;
    QTimer::singleShot(120, this, [this, generation] {
        takeWindowPicture(generation, 1);
    });
    QTimer::singleShot(1300, this, [this, generation] {
        takeWindowPicture(generation, 2);
    });
}

void SpectacleCore::takeWindowPicture(int generation, int pass)
{
    // cancelled, captured, or a newer resize meanwhile: nothing to do
    if (generation != m_retakeGeneration || !m_retaking || CaptureWindow::instances().isEmpty()) {
        return;
    }
    // the quiet second picture never wipes marks drawn since the first
    if (pass == 2 && m_annotationDocument->undoStackDepth() > 0) {
        m_retaking = false;
        return;
    }
    const auto window = WindowPicker::instance()->window(m_retakeUuid);
    if (!window) {
        m_retaking = false;
        return;
    }
    // the resized window, then every window stacked above it that overlaps it, so they
    // stay on top in the picture as they are on screen
    QList<WindowPicker::Window> layers{*window};
    for (const auto &above : WindowPicker::instance()->windowsAbove(m_retakeUuid)) {
        if (above.geometry.intersects(window->geometry)) {
            layers << above;
        }
    }
    auto shots = std::make_shared<QList<std::pair<QRect, QImage>>>();
    auto pending = std::make_shared<int>(layers.size());
    auto failed = std::make_shared<bool>(false);
    const ImagePlatformKWin::ScreenShotFlags flags = ImagePlatformKWin::ScreenShotFlag::IncludeDecoration | ImagePlatformKWin::ScreenShotFlag::NativeSize;
    for (int i = 0; i < layers.size(); ++i) {
        shots->append({layers[i].geometry, QImage()});
        auto source = new ScreenShotSourceWindow2(layers[i].uuid, flags);
        connect(source, &ScreenShotSource2::finished, this, [this, source, i, shots, pending, failed, generation, pass](const ResultVariant &result) {
            source->deleteLater();
            if (result.index() == ResultVariant::Image) {
                (*shots)[i].second = std::get<ResultVariant::Image>(result);
            } else if (i == 0) {
                *failed = true; // the resized window itself
            }
            if (--*pending > 0) {
                return;
            }
            if (generation != m_retakeGeneration || !m_retaking || CaptureWindow::instances().isEmpty()) {
                return;
            }
            if (pass == 2) {
                m_retaking = false;
                if (m_annotationDocument->undoStackDepth() > 0) {
                    return; // marks were drawn while it was being taken
                }
            }
            if (*failed) {
                if (pass == 1) {
                    showErrorMessage(i18nc("@info", "Couldn't take the resized window's picture."));
                }
                return;
            }
            // into the frozen picture, each at its window's place, bottom to top
            QImage picture = m_annotationDocument->baseImage();
            const qreal dpr = m_annotationDocument->imageDpr() > 0 ? m_annotationDocument->imageDpr() : 1.0;
            const QPointF origin = m_annotationDocument->canvasRect().topLeft();
            {
                QPainter painter(&picture);
                painter.setRenderHint(QPainter::SmoothPixmapTransform);
                const qreal k = dpr / picture.devicePixelRatio();
                for (const auto &[frame, shot] : std::as_const(*shots)) {
                    if (shot.isNull()) {
                        continue;
                    }
                    const QRectF target(QPointF(frame.topLeft()) - origin, QSizeF(frame.size()));
                    painter.drawImage(QRectF(target.topLeft() * k, target.size() * k), shot);
                }
            }
            if (pass == 1) {
                m_annotationDocument->clearAnnotations();
            }
            m_annotationDocument->setBaseImage(picture);
            setExportImage(picture);
            if (pass == 1) {
                SelectionEditor::instance()->pickWindow(m_retakeUuid);
                // the marks are gone and the window is its new size: the frame's history starts here
                FrameHistory::instance()->reset();
            }
            CaptureTools::instance()->scanFrozenPicture();
            TestScript::report(u"ECTOLENS-RETAKEN %1 %2"_s.arg(pass).arg(CaptureTools::instance()->msSinceResize()));
        });
    }
}

ImagePlatform::GrabMode SpectacleCore::toGrabMode(CaptureModeModel::CaptureMode captureMode) const
{
    using GrabMode = ImagePlatform::GrabMode;
    using CaptureMode = CaptureModeModel::CaptureMode;
    const auto &supportedGrabModes = m_imagePlatform->supportedGrabModes();
    if (captureMode == CaptureMode::CurrentScreen
        && supportedGrabModes.testFlag(ImagePlatform::CurrentScreen)) {
        return GrabMode::CurrentScreen;
    } else if (captureMode == CaptureMode::ActiveWindow
        && supportedGrabModes.testFlag(ImagePlatform::ActiveWindow)) {
        return GrabMode::ActiveWindow;
    } else if (captureMode == CaptureMode::WindowUnderCursor
        && supportedGrabModes.testFlag(ImagePlatform::WindowUnderCursor)) {
        if (supportedGrabModes.testFlag(ImagePlatform::TransientWithParent)) {
            return GrabMode::TransientWithParent;
        } else {
            return GrabMode::WindowUnderCursor;
        }
    } else if (captureMode == CaptureMode::RectangularRegion
        && supportedGrabModes.testFlag(ImagePlatform::PerScreenImageNative)) {
        return GrabMode::PerScreenImageNative;
    } else if (captureMode == CaptureMode::AllScreensScaled
        && supportedGrabModes.testFlag(ImagePlatform::AllScreensScaled)) {
        return GrabMode::AllScreensScaled;
    } else if (supportedGrabModes.testFlag(ImagePlatform::AllScreens)) { // default if supported
        return GrabMode::AllScreens;
    } else {
        return GrabMode::NoGrabModes;
    }
}

CaptureModeModel::CaptureMode SpectacleCore::toCaptureMode(ImagePlatform::GrabMode grabMode) const
{
    using GrabMode = ImagePlatform::GrabMode;
    using CaptureMode = CaptureModeModel::CaptureMode;
    if (grabMode == GrabMode::CurrentScreen) {
        return CaptureMode::CurrentScreen;
    } else if (grabMode == GrabMode::ActiveWindow) {
        return CaptureMode::ActiveWindow;
    } else if (grabMode == GrabMode::WindowUnderCursor) {
        return CaptureMode::WindowUnderCursor;
    } else if (grabMode == GrabMode::PerScreenImageNative) {
        return CaptureMode::RectangularRegion;
    } else if (grabMode == GrabMode::AllScreensScaled) {
        return CaptureMode::AllScreensScaled;
    } else {
        if (m_imagePlatform->supportedGrabModes().testFlag(ImagePlatform::CurrentScreen)) {
            return CaptureMode::AllScreens;
        } else {
            return CaptureMode::FullScreen;
        }
    }
}

bool SpectacleCore::isGuiNull() const
{
    return SpectacleWindow::instances().isEmpty();
}

void SpectacleCore::initGuiNoScreenshot()
{
    initViewerWindow(ViewerWindow::Dialog);
    ViewerWindow::instance()->setVisible(true);
}

// Hurry up the sync if the sync timer is active.
void SpectacleCore::syncExportImage()
{
    if (!m_annotationSyncTimer->isActive()) {
        return;
    }
    auto image = m_annotationDocument->renderToImage();
    auto canvasPos = m_annotationDocument->canvasRect().topLeft();
    ImageMetaData::setLogicalXY(image, canvasPos.x(), canvasPos.y());
    setExportImage(image);
}

// A convenient way to stop the sync timer and set the export image.
void SpectacleCore::setExportImage(const QImage &image)
{
    m_annotationSyncTimer->stop();
    ExportManager::instance()->setImage(image);
}

QQmlEngine *SpectacleCore::getQmlEngine()
{
    if (m_engine == nullptr) {
        m_engine = std::make_unique<QQmlEngine>(this);
        m_engine->rootContext()->setContextObject(new KLocalizedQmlContext(m_engine.get()));
    }
    return m_engine.get();
}

void SpectacleCore::initCaptureWindows(CaptureWindow::Mode mode)
{
    deleteWindows();

    // Allow the window to be transparent. Used for video recording UI.
    // It has to be set before creating the window.
    QQuickWindow::setDefaultAlphaBuffer(true);

    auto engine = getQmlEngine();
    const auto screens = qApp->screens();
    for (auto *screen : screens) {
        const auto screenRect = Geometry::mapFromPlatformRect(screen->geometry(), screen->devicePixelRatio());
        // Don't show windows for screens that don't have an image.
        if (!m_videoMode
            && !m_annotationDocument->baseImage().isNull()
            && !screenRect.intersects(m_annotationDocument->canvasRect())) {
            continue;
        }
        m_captureWindows.emplace_back(CaptureWindow::makeUnique(mode, screen, engine));
    }
}

void SpectacleCore::initViewerWindow(ViewerWindow::Mode mode)
{
    // always switch to gui mode when a viewer window is used.
    m_startMode = SpectacleCore::StartMode::Gui;
    m_returnToViewer = true;
    deleteWindows();

    // Transparency isn't needed for this window.
    QQuickWindow::setDefaultAlphaBuffer(false);

    m_viewerWindow = ViewerWindow::makeUnique(mode, getQmlEngine());
}

void SpectacleCore::deleteWindows()
{
    m_viewerWindow.reset();
    m_captureWindows.clear();
}

void SpectacleCore::unityLauncherUpdate(const QVariantMap &properties) const
{
    QDBusMessage message = QDBusMessage::createSignal(u"/org/kde/Ectolens"_s,
                                                      u"com.canonical.Unity.LauncherEntry"_s,
                                                      u"Update"_s);
    message.setArguments({QApplication::desktopFileName(), properties});
    QDBusConnection::sessionBus().send(message);
}

void SpectacleCore::startRecording(VideoPlatform::RecordingMode mode, bool withPointer)
{
    if (m_videoPlatform->isRecording() || mode == VideoPlatform::NoRecordingModes) {
        return;
    }
    const bool hasCaptureWindows = !CaptureWindow::instances().empty();
    if (hasCaptureWindows && mode != VideoPlatform::Region && !m_recordingEventLoopLocker) {
        m_recordingEventLoopLocker = std::make_unique<QEventLoopLocker>();
    }
    if (hasCaptureWindows) {
        SpectacleWindow::setVisibilityForAll(QWindow::Hidden);
        if (mode != VideoPlatform::Region) {
            m_returnToViewer = true;
        }
    }
    m_lastRecordingMode = mode;
    setVideoMode(true);
    const auto &output = m_outputUrl.isLocalFile() ? videoOutputUrl() : QUrl();
    m_videoPlatform->startRecording(output, mode, {}, withPointer);
}

void SpectacleCore::recordSelection()
{
    if (m_videoPlatform->isRecording() || !(m_videoPlatform->supportedRecordingModes() & VideoPlatform::Region) || CaptureWindow::instances().isEmpty()) {
        return;
    }
    // In video mode the overlay's frozen picture (with any marks drawn on it)
    // fades out over Kirigami's very long duration; accept once it has gone, and
    // the accepted handler records the frame as a region recording.
    setVideoMode(true);
    if (!m_recordDelay) {
        m_recordDelay = new QTimer(this);
        m_recordDelay->setSingleShot(true);
        m_recordDelay->setInterval(450);
        connect(m_recordDelay, &QTimer::timeout, this, [] {
            // Esc or Close during the fade closed the overlay: nothing to record then
            if (!CaptureWindow::instances().isEmpty()) {
                SelectionEditor::instance()->acceptSelection();
            }
        });
    }
    m_recordDelay->start();
}

void SpectacleCore::finishRecording()
{
    Q_ASSERT(m_videoPlatform->isRecording());
    m_videoPlatform->finishRecording();
}

bool SpectacleCore::videoMode() const
{
    return m_videoMode;
}

void SpectacleCore::setVideoMode(bool videoMode)
{
    if (videoMode == m_videoMode) {
        return;
    }
    InlineMessageModel::instance()->clear();
    m_videoMode = videoMode;
    if (!videoMode && m_annotationDocument->baseImage().isNull()) {
        // Change this if there ends up being a way to toggle video mode outside of rectangle capture mode.
        takeNewScreenshot(ImagePlatform::PerScreenImageNative, 0, Settings::includePointer(), Settings::includeDecorations(), Settings::includeShadow());
    }
    Q_EMIT videoModeChanged(videoMode);
}

QUrl SpectacleCore::currentVideo() const
{
    return m_currentVideo;
}

void SpectacleCore::setCurrentVideo(const QUrl &currentVideo)
{
    if (currentVideo == m_currentVideo) {
        return;
    }
    m_currentVideo = currentVideo;
    Q_EMIT currentVideoChanged(currentVideo);
}

QUrl SpectacleCore::videoOutputUrl() const
{
    return VideoPlatform::formatForPath(m_outputUrl.path()) != VideoPlatform::NoFormat ? m_outputUrl : QUrl();
}

QString SpectacleCore::recordedTime() const
{
    return timeFromMilliseconds(m_videoPlatform->recordedTime());
}

QString SpectacleCore::timeFromMilliseconds(qint64 milliseconds) const
{
    KFormat::DurationFormatOptions options = KFormat::DefaultDuration;
    if (milliseconds < 1000.0 * 60.0 * 60.0) {
        options |= KFormat::FoldHours;
    }
    return KFormat().formatDuration(milliseconds, options);
}

void SpectacleCore::activateAction(const QString &actionName, const QVariant &parameter)
{
    Q_UNUSED(parameter)
    if (!m_videoPlatform->isRecording() && closeOverlayOnSecondPress()) {
        return;
    }
    m_startMode = StartMode::DBus;
    if (m_videoPlatform->isRecording()) {
        // BUG: https://bugs.kde.org/show_bug.cgi?id=481471
        // TODO: find a way to support screenshot shortcuts while recording?
        finishRecording();
    } else if (actionName == ShortcutActions::self()->fullScreenAction()->objectName()) {
        takeNewScreenshot(CaptureModeModel::AllScreens, 0);
    } else if (actionName == ShortcutActions::self()->currentScreenAction()->objectName()) {
        takeNewScreenshot(CaptureModeModel::CurrentScreen, 0);
    } else if (actionName == ShortcutActions::self()->activeWindowAction()->objectName()) {
        takeNewScreenshot(CaptureModeModel::ActiveWindow, 0);
    } else if (actionName == ShortcutActions::self()->windowUnderCursorAction()->objectName()) {
        takeNewScreenshot(CaptureModeModel::WindowUnderCursor, 0);
    } else if (actionName == ShortcutActions::self()->regionAction()->objectName()) {
        takeNewScreenshot(CaptureModeModel::RectangularRegion, 0);
    } else if (actionName == ShortcutActions::self()->recordRegionAction()->objectName()) {
        startRecording(VideoPlatform::Region);
    } else if (actionName == ShortcutActions::self()->recordScreenAction()->objectName()) {
        startRecording(VideoPlatform::Screen);
    } else if (actionName == ShortcutActions::self()->recordWindowAction()->objectName()) {
        startRecording(VideoPlatform::Window);
    } else if (actionName == ShortcutActions::self()->openWithoutScreenshotAction()->objectName()) {
        initGuiNoScreenshot();
    }
}

#include "moc_SpectacleCore.cpp"
