/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QRectF>
#include <QUrl>
#include <QVariantList>

/**
 * Ectolens: what the redesigned overlay and window (design proposal 2) ask of the app,
 * beyond what SpectacleCore already offers:
 *
 * - the frame label's size menu: the screen's size, and setting the frame to a size
 *   (on a window, the window itself resizes through KWin's scripting and is put back afterward),
 * - Pick Color, Open QR Code, Hide Private Info and Pin,
 * - the lists behind Open with and Share,
 * - the Record panel's choices,
 * - Settings, Keyboard Shortcuts, Help, Report a Bug and About.
 */
class CaptureTools : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// What a QR code in the frozen picture says, and where it is (logical, global).
    Q_PROPERTY(QString qrText READ qrText NOTIFY qrChanged FINAL)
    Q_PROPERTY(QRectF qrRect READ qrRect NOTIFY qrChanged FINAL)
    Q_PROPERTY(bool qrIsLink READ qrIsLink NOTIFY qrChanged FINAL)
    /// Hide Private Info is reading the frame.
    Q_PROPERTY(bool hidingPrivateInfo READ hidingPrivateInfo NOTIFY hidingPrivateInfoChanged FINAL)
    /// What the last Hide Private Info found, for its message ("Blurred 3 private items.").
    Q_PROPERTY(QString hideResult READ hideResult NOTIFY hidingPrivateInfoChanged FINAL)
    /// Ectolens: how many private items Hide Private Info's last reading found; -1 before a
    /// reading, or when the text reader didn't start
    Q_PROPERTY(int privateInfoFound READ privateInfoFound NOTIFY hidingPrivateInfoChanged FINAL)
    /// A window is being resized and the picture retaken.
    Q_PROPERTY(bool resizing READ resizing NOTIFY resizingChanged FINAL)
    /// Why the last size could not be set exactly, shown in the size menu ("Konsole can't be 1920×1080. It stopped at 1600×900.").
    Q_PROPERTY(QString sizeProblem READ sizeProblem NOTIFY sizeProblemChanged FINAL)
    /// Pick Color or Measure, shared by every screen's overlay ("" when neither).
    Q_PROPERTY(QString overlayMode READ overlayMode WRITE setOverlayMode NOTIFY overlayModeChanged FINAL)
    /// Where the picture in the Ectolens window was last saved (empty until it is).
    Q_PROPERTY(QUrl savedUrl READ savedUrl NOTIFY savedUrlChanged FINAL)
    /// The size a resized window had before, to put back.
    Q_PROPERTY(QSize originalSize READ originalSize NOTIFY resizingChanged FINAL)

public:
    static CaptureTools *instance();
    static CaptureTools *create(QQmlEngine *engine, QJSEngine *)
    {
        auto inst = instance();
        Q_ASSERT(inst->thread() == engine->thread());
        QJSEngine::setObjectOwnership(inst, QJSEngine::CppOwnership);
        return inst;
    }

    QString qrText() const;
    QRectF qrRect() const;
    bool qrIsLink() const;
    bool hidingPrivateInfo() const;
    QString hideResult() const;
    bool resizing() const;
    QString sizeProblem() const;
    QSize originalSize() const;

    // ── The size menu ──
    /// The size in pixels of the screen holding most of a logical rect.
    Q_INVOKABLE QSize screenPixelSize(const QRectF &logicalRect) const;
    /// The scale of that screen.
    Q_INVOKABLE qreal screenScale(const QRectF &logicalRect) const;
    /// The screens, for the whole-screen frame's menu: [{name, width, height, rect}]
    Q_INVOKABLE QVariantList screens() const;
    /// Frame a whole screen.
    Q_INVOKABLE void frameScreen(int index);
    /// Set the frame to a size in pixels. A window taken by clicking resizes to it (when
    /// "Resize to fit" is checked) and the picture is taken again; any other frame takes the size.
    Q_INVOKABLE void setFrameSize(int pixelWidth, int pixelHeight);
    /// Put a window resized from the size menu back to its old size (when "Revert size afterward" is checked).
    Q_INVOKABLE void revertResizedWindow();

    // ── Pick Color ──
    Q_INVOKABLE QColor colorAt(const QPointF &logicalGlobal) const;
    Q_INVOKABLE void copyText(const QString &text) const;

    // ── Open QR Code ──
    /// Look for a QR code in the frozen picture (called when the overlay opens).
    void scanFrozenPicture();
    Q_INVOKABLE void openQr() const;
    Q_INVOKABLE void copyQr() const;

    // ── Hide Private Info ──
    /// Read the words in the frame and blur email addresses, keys, tokens and home-folder paths.
    Q_INVOKABLE void hidePrivateInfo(const QRectF &logicalFrame);
    /// Ectolens: whether Hide Private Info has finished with all of this frame of the frozen
    /// picture now showing: read it and blurred what it found, or found the text reader would not
    /// start (privateInfoReaderRan says which). An agent's capture goes only after this.
    bool privateInfoReadFor(const QRectF &logicalFrame) const;
    bool privateInfoReaderRan() const;
    int privateInfoFound() const;
    /// Ectolens: the blurs Hide Private Info's last reading made come off (the agent dialog's
    /// "Blur private information" unchecked), through the picture's own undo, or a reading still
    /// going is dropped. putBackPrivateInfoBlurs() puts them back with redo, or reads the frame
    /// again when the picture has changed since.
    void takeOffPrivateInfoBlurs();
    void putBackPrivateInfoBlurs(const QRectF &logicalFrame);
    /// The rule behind Hide Private Info, public so tests can hold it to examples.
    static bool looksPrivate(const QString &word);
    /// Ectolens: the words in a picture, read with Tesseract as Extract Text reads them, for an
    /// agent; with hidePrivate, what looks private reads "[hidden]".
    static QString readText(const QImage &image, bool hidePrivate);

    // ── Pin ──
    /// Float what is framed above your windows, and close the overlay.
    Q_INVOKABLE void pin(const QRectF &logicalFrame);

    // ── Open with and Share ──
    /// Apps that open images: [{id, name, icon}]. The Ectolens window is id "".
    Q_INVOKABLE QVariantList openWithApps() const;
    Q_INVOKABLE QString appName(const QString &storageId) const;
    /// KDE's share services for an image: [{id, name, icon}]
    Q_INVOKABLE QVariantList shareTargets() const;
    Q_INVOKABLE QString shareTargetName(const QString &pluginId) const;
    void openWith(const QString &storageId, const QUrl &url) const;
    void share(const QString &pluginId, const QUrl &url);
    Q_INVOKABLE void showInFolder(const QUrl &url) const;
    /// The Ectolens window's Save › Send to: the saved file, or a temporary copy while unsaved.
    Q_INVOKABLE void openCurrentWith(const QString &storageId);
    Q_INVOKABLE void shareCurrent(const QString &pluginId);
    /// Copy › File Path: the saved file's path (locked in the menu until it is saved).
    Q_INVOKABLE void copyCurrentPath() const;
    QUrl savedUrl() const;
    QString overlayMode() const;
    /// Milliseconds since the size menu last asked for a size (for the off-screen test's timing).
    qint64 msSinceResize() const;
    void setOverlayMode(const QString &mode);
    void setSavedUrl(const QUrl &url);

    // ── The Record panel ──
    Q_INVOKABLE QVariantList frameRates() const;
    Q_INVOKABLE QString qualityNote(int quality) const;
    Q_INVOKABLE QVariantList videoFormats() const;
    Q_INVOKABLE bool formatHasSound(int format) const;

    // ── Keys ──
    /// A global shortcut as set in System Settings: "open" (Print Screen), "fullScreen",
    /// "activeWindow", "region"; empty when it has none.
    Q_INVOKABLE QString globalShortcut(const QString &which) const;
    /// A key combination written the way this desktop writes keys ("Ctrl+Shift+S").
    Q_INVOKABLE QString keyText(const QString &portable) const;

    // ── Settings and help ──
    Q_INVOKABLE void openSettings(const QString &page = QString()) const;
    Q_INVOKABLE void openShortcuts() const;
    /// "help", "bug" or "about"
    Q_INVOKABLE void help(const QString &which) const;

Q_SIGNALS:
    void qrChanged();
    void hidingPrivateInfoChanged();
    void resizingChanged();
    void sizeProblemChanged();
    void savedUrlChanged();
    void overlayModeChanged();

private:
    explicit CaptureTools(QObject *parent = nullptr);
    void runKWinScript(const QString &source);
    void finishResize(const QString &uuid, const QSize &wanted, int triesLeft, int generation);
    void setSizeProblem(const QString &text);

    QString m_qrText;
    QRectF m_qrRect;
    bool m_hiding = false;
    QString m_hideResult;
    bool m_resizing = false;
    QString m_sizeProblem;
    // the window resized from the size menu: its id and its frame before (logical)
    QString m_resizedUuid;
    QRect m_resizedFrom;
    int m_scriptCount = 0;
    int m_resizeGeneration = 0; // bumped when a resize is no longer wanted
    int m_pictureGeneration = 0; // bumped with each new frozen picture
    // what Hide Private Info last finished reading: the frame (logical), the picture it was in and
    // whether the text reader ran
    int m_frozenPicture = 0; // bumped with each new frozen picture only
    int m_readPicture = -1;
    QRectF m_readFrame;
    bool m_readRan = false;
    // the picture's undo depth before and after the last reading's blurs, and whether they're off
    int m_blurDepthBefore = -1;
    int m_blurDepthAfter = -1;
    int m_blurFound = -1;
    bool m_blursOff = false;
    bool m_resizedWasMaximized = false;
    QUrl m_savedUrl;
    QString m_overlayMode;
    QElapsedTimer m_resizeClock;
};
