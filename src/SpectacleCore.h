/* This file is part of Spectacle, the KDE screenshot utility
 * SPDX-FileCopyrightText: 2019 Boudhayan Gupta <bgupta@kde.org>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include <QEventLoopLocker>
#include <QObject>
#include <QQmlEngine>
#include <QQuickItem>
#include <QVariantAnimation>

#include "CaptureModeModel.h"
#include "CommandLineOptions.h"
#include "ExportManager.h"
#include "Gui/CaptureWindow.h"
#include "Gui/ViewerWindow.h"
#include "OcrManager.h"
#include "Platforms/PlatformLoader.h"
#include "RecordingModeModel.h"
#include "VideoFormatModel.h"
#include "settings.h"

#include <KQuickImageEditor/AnnotationDocument>

#include <QJsonObject>

#include <array>
#include <functional>
#include <memory>
#include <optional>

class SpectacleCore : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(ImagePlatform *imagePlatform READ imagePlatform CONSTANT FINAL)
    Q_PROPERTY(VideoPlatform *videoPlatform READ videoPlatform CONSTANT FINAL)
    Q_PROPERTY(QUrl screenCaptureUrl READ screenCaptureUrl NOTIFY screenCaptureUrlChanged FINAL)
    Q_PROPERTY(int captureTimeRemaining READ captureTimeRemaining NOTIFY captureTimeRemainingChanged FINAL)
    Q_PROPERTY(qreal captureProgress READ captureProgress NOTIFY captureProgressChanged FINAL)
    Q_PROPERTY(QString recordedTime READ recordedTime NOTIFY recordedTimeChanged)
    Q_PROPERTY(bool videoMode READ videoMode WRITE setVideoMode NOTIFY videoModeChanged)
    Q_PROPERTY(QUrl currentVideo READ currentVideo NOTIFY currentVideoChanged)
    Q_PROPERTY(AnnotationDocument *annotationDocument READ annotationDocument CONSTANT FINAL)
    Q_PROPERTY(bool ocrAvailable READ ocrAvailable NOTIFY ocrStatusChanged FINAL)
    Q_PROPERTY(OcrManager::OcrStatus ocrStatus READ ocrStatus NOTIFY ocrStatusChanged FINAL)
    /// Ectolens: an AI agent asks for a capture or a recording, and the overlay waits for the
    /// person to send it or not (AgentService, ectolens-mcp)
    Q_PROPERTY(bool agentAsking READ agentAsking NOTIFY agentChanged FINAL)
    /// "capture" or "record"
    Q_PROPERTY(QString agentKind READ agentKind NOTIFY agentChanged FINAL)
    /// Ectolens: whether this request's picture is blurred where it looks private (the dialog's
    /// "Blur private information"); starts as Settings › Model Context Protocol (MCP) says
    Q_PROPERTY(bool agentBlur READ agentBlur WRITE setAgentBlur NOTIFY agentChanged FINAL)
    /// the agent's name, as it gives it ("Claude Code"), and why it asks
    Q_PROPERTY(QString agentClient READ agentClient NOTIFY agentChanged FINAL)
    Q_PROPERTY(QString agentReason READ agentReason NOTIFY agentChanged FINAL)
    /// the person set the request's dialog aside to adjust the frame (Adjust First)
    Q_PROPERTY(bool agentAdjusting READ agentAdjusting NOTIFY agentChanged FINAL)

public:
    enum class StartMode {
        Gui = 0,
        DBus = 1,
        Background = 2,
    };

    ~SpectacleCore() noexcept override;

    static SpectacleCore *instance();

    ImagePlatform *imagePlatform() const;
    VideoPlatform *videoPlatform() const;

    AnnotationDocument *annotationDocument() const;

    QUrl screenCaptureUrl() const;
    void setScreenCaptureUrl(const QUrl &url);
    // Used when setting the URL from CLI
    void setScreenCaptureUrl(const QString &filePath);

    QUrl outputUrl() const;

    int captureTimeRemaining() const;
    qreal captureProgress() const;

    QString recordedTime() const;

    bool videoMode() const;
    void setVideoMode(bool enabled);

    QUrl currentVideo() const;

    bool ocrAvailable() const;
    OcrManager::OcrStatus ocrStatus() const;
    Q_INVOKABLE QVariantMap ocrAvailableLanguages() const;
    Q_INVOKABLE bool startOcrExtraction(const QString &languageCode = QString());

    void initGuiNoScreenshot();

    void syncExportImage();

    Q_INVOKABLE void startRecording(VideoPlatform::RecordingMode mode, bool withPointer = Settings::videoIncludePointer());
    Q_INVOKABLE void finishRecording();
    /// Ectolens: record what is framed in the open overlay. The frozen picture
    /// fades into the live screen, then the frame is recorded.
    Q_INVOKABLE void recordSelection();

    Q_INVOKABLE QString timeFromMilliseconds(qint64 milliseconds) const;

    /// Ectolens: after the frame label's size menu resized a window, take the frozen
    /// picture again and frame that window where it is now.
    void retakeFrozenPicture(const QString &reframeWindowUuid);
    /// Ectolens: the next capture comes from a D-Bus API call (FullScreen, ActiveWindow…), not a
    /// person: it keeps Spectacle's promise of a saved file and skips After Capture's extras.
    void markNextCaptureAsApiCall();
    /// Ectolens: a capture key pressed while the overlay is up closes it. Returns true if it did.
    bool closeOverlayOnSecondPress();
    /// Ectolens: close the overlay without exporting or opening the window (Pin).
    void closeOverlayQuietly();
    /// Ectolens: Capture with Share set to ask each time: the overlay asked where to share;
    /// finish the capture with that service, or without sharing if empty.
    Q_INVOKABLE void captureSharingWith(const QString &pluginId);

    ExportManager::Actions autoExportActions() const;

    void activateAction(const QString &actionName, const QVariant &parameter);

    static SpectacleCore *create(QQmlEngine *engine, QJSEngine *)
    {
        auto inst = instance();
        Q_ASSERT(inst);
        Q_ASSERT(inst->thread() == engine->thread());

        if (s_qmlEngine) {
            Q_ASSERT(engine == s_qmlEngine);
        } else {
            s_qmlEngine = engine;
        }

        QJSEngine::setObjectOwnership(inst, QJSEngine::CppOwnership);
        return inst;
    }

public Q_SLOTS:
    void activate(const QStringList &arguments, const QString &workingDirectory);
    void takeNewScreenshot(int captureMode = Settings::captureMode(),
                           int timeout = Settings::captureOnClick() ? -1 : Settings::captureDelay() * 1000,
                           bool includePointer = Settings::includePointer(),
                           bool includeDecorations = Settings::includeDecorations(),
                           bool includeShadow = Settings::includeShadow());
    void cancelScreenshot();
    void showErrorMessage(const QString &message);

public:
    /// Ectolens: an agent's request (AgentService) and how to answer it.
    struct AgentRequest {
        QString kind; // "capture" or "record"
        QString target; // "screen", "window", "active_window" or "area"
        QString windowId;
        QRectF area;
        QString reason;
        QString client;
        bool wantText = false;
    };
    using AgentReply = std::function<void(const QJsonObject &)>;
    /// Open the overlay framed on what the agent asked for; reply once the person sends it or not.
    void agentCapture(const AgentRequest &request, AgentReply reply);
    /// Stop the recording an agent started; reply with its file once it is written.
    void agentRecordStop(AgentReply reply);
    bool agentAsking() const;
    QString agentKind() const;
    bool agentBlur() const;
    void setAgentBlur(bool blur);
    QString agentClient() const;
    QString agentReason() const;
    /// the overlay's Send and Don't Send
    Q_INVOKABLE void agentSend();
    Q_INVOKABLE void agentRefuse();
    Q_INVOKABLE void agentAdjust();
    bool agentAdjusting() const;

Q_SIGNALS:
    void screenCaptureUrlChanged();
    void captureTimeRemainingChanged();
    void captureProgressChanged();

    void allDone();
    void dbusScreenshotFailed(const QString &message);
    void dbusRecordingFailed(const QString &message);
    void videoModeChanged(bool videoMode);
    void currentVideoChanged(const QUrl &currentVideo);
    void recordedTimeChanged();
    void ocrStatusChanged();
    /// Ectolens: Capture needs a share service picked in the overlay before it finishes.
    void shareTargetNeeded();
    void agentChanged();

private:
    explicit SpectacleCore(QObject *parent = nullptr);

    enum class ScreenCapture {
        Screenshot,
        Recording,
    };

    void takeNewScreenshot(ImagePlatform::GrabMode grabMode, int timeout, bool includePointer, bool includeDecorations, bool includeShadow);
    void setExportImage(const QImage &image);
    void showViewerIfGuiMode(bool minimized = false);
    void doNotify(ScreenCapture type, const ExportManager::Actions &actions, const QUrl &saveUrl);
    ImagePlatform::GrabMode toGrabMode(CaptureModeModel::CaptureMode captureMode) const;
    CaptureModeModel::CaptureMode toCaptureMode(ImagePlatform::GrabMode grabMode) const;
    bool isGuiNull() const;
    QQmlEngine *getQmlEngine();
    void initCaptureWindows(CaptureWindow::Mode mode);
    void initViewerWindow(ViewerWindow::Mode mode);
    void deleteWindows();
    void unityLauncherUpdate(const QVariantMap &properties) const;
    void setCurrentVideo(const QUrl &currentVideo);
    QUrl videoOutputUrl() const;
    bool performOcrExtraction(const QString &languageCode);

    static SpectacleCore *s_self;
    std::unique_ptr<AnnotationDocument> m_annotationDocument = nullptr;
    StartMode m_startMode = StartMode::Gui;
    bool m_returnToViewer = false;
    bool m_ocrExportInProgress = false;
    bool m_quitAfterOcr = false;
    QUrl m_screenCaptureUrl;
    std::unique_ptr<ImagePlatform> m_imagePlatform;
    std::unique_ptr<VideoPlatform> m_videoPlatform;
    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QTimer> m_annotationSyncTimer;
    std::unique_ptr<QVariantAnimation> m_delayAnimation;
    std::unique_ptr<QEventLoopLocker> m_eventLoopLocker;
    std::unique_ptr<QEventLoopLocker> m_recordingEventLoopLocker;

    // Use ViewerWindow::instance() to get the viewer window.
    ViewerWindow::UniquePointer m_viewerWindow = {nullptr, nullptr};

    // Use CaptureWindow::instances() to get the capture windows.
    // Don't assume that this will never have entries that are null.
    // For some reason, removeIf/erase_if/find_if then erase doesn't work with QList/QList,
    // so we have to use std::vector. Something about use of a deleted unique_ptr function.
    std::vector<CaptureWindow::UniquePointer> m_captureWindows;

    std::array<bool, CommandLineOptions::TotalOptions> m_cliOptions = {};

    QUrl m_editExistingUrl;
    QUrl m_outputUrl;

    ImagePlatform::GrabMode m_lastGrabMode = ImagePlatform::GrabMode::NoGrabModes;
    bool m_lastIncludePointer = false; // cli default value
    bool m_lastIncludeDecorations = true; // cli default value
    bool m_lastIncludeShadow = true; // cli default value
    VideoPlatform::RecordingMode m_lastRecordingMode = VideoPlatform::NoRecordingModes;
    bool m_videoMode = false;
    QUrl m_currentVideo;

    // Ectolens: After Capture (design proposal 2). The list behind Capture's arrow runs
    // when a capture is finished with Enter or Capture: the clipboard and the file through
    // ExportManager's actions, and these after the export.
    struct AfterCapture {
        bool pending = false;
        bool open = false;
        QString openApp; // empty: the Ectolens window
        bool showInFolder = false;
        bool share = false;
        QString shareTarget;
    };
    AfterCapture m_afterCapture;
    bool m_shareAsked = false; // the overlay already asked where to share
    QString m_shareOnce; // the service picked there
    QString m_captureTitle; // "Konsole 799×508", the notification's title
    bool m_captureFromWindow = false; // the capture started with the Ectolens window open
    bool m_retaking = false; // the size menu is taking the picture again
    int m_retakeGeneration = 0; // bumped by each resize; older pictures are dropped
    void takeWindowPicture(int generation, int pass);
    bool m_nextIsApi = false;
    bool m_apiCapture = false; // this capture came from a D-Bus API call
    QTimer *m_recordDelay = nullptr; // Record's fade before the recording starts
    QString m_retakeUuid; // the window to frame once it has
    void runAfterCapture(const ExportManager::Actions &actions, const QUrl &url);
    QString captureTitle(const QRectF &rect) const;

    // Ectolens: the agent's request being answered, and its recording
    std::optional<AgentRequest> m_agent;
    AgentReply m_agentReply;
    AgentReply m_agentStopReply;
    bool m_agentRecording = false;
    bool m_agentAdjusting = false;
    // Send was pressed on a frame Hide Private Info had not read yet: it goes once the reading ends
    bool m_agentSendWhenRead = false;
    bool m_agentBlur = true;
    QString m_agentLastRecording;
    void answerAgent(const QJsonObject &result);
    void frameForAgent();
    void sendToAgent(const QRectF &rect);
    // after an agent's round, Ectolens waits a while for the next before it quits
    QTimer *m_agentIdle = nullptr;
    void agentIdle();

    static inline QQmlEngine *s_qmlEngine = nullptr;
};
