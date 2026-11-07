/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#include "CaptureTools.h"

#include "Config.h"

#include "ExportManager.h"
#include "Gui/HelpMenu.h"
#include "Gui/PinWindow.h"
#include "Gui/SelectionEditor.h"
#include "Gui/SettingsDialog/SettingsDialog.h"
#include "Platforms/RecordingQuality.h"
#include "Platforms/VideoPlatform.h"
#include "Platforms/WindowPicker.h"
#include "PrivateInfo.h"
#include "ShortcutActions.h"
#include "SpectacleCore.h"
#include "TestScript.h"
#include "settings.h"
#include "spectacle_debug.h"

#include <KApplicationTrader>
#include <KGlobalAccel>
#include <KHelpMenu>
#include <KWindowSystem>
#include <KIO/ApplicationLauncherJob>
#include <KIO/JobUiDelegateFactory>
#include <KIO/OpenFileManagerWindowJob>
#include <KIO/OpenUrlJob>
#include <KLocalizedString>
#include <KService>
#include <KSystemClipboard>
#include <Prison/ImageScanner>
#include <Prison/ScanResult>

#ifdef PURPOSE_FOUND
#include <Purpose/AlternativesModel>
#include <Purpose/Menu>
#endif

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QJsonArray>
#include <QMimeData>
#include <optional>
#include <QEventLoopLocker>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QScreen>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTimer>
#include <QtConcurrentRun>

#include <tesseract/baseapi.h>
#include <tesseract/resultiterator.h>

using namespace Qt::StringLiterals;

namespace
{
// The screen holding most of a logical rect.
QScreen *screenFor(const QRectF &rect)
{
    QScreen *best = QGuiApplication::primaryScreen();
    qreal bestArea = -1;
    const QPointF center = rect.center();
    for (auto screen : QGuiApplication::screens()) {
        const QRectF overlap = QRectF(screen->geometry()).intersected(rect);
        const qreal area = overlap.width() * overlap.height();
        if (area > bestArea || (area == bestArea && screen->geometry().contains(center.toPoint()))) {
            best = screen;
            bestArea = area;
        }
    }
    return best;
}

// The picture under the overlay: the frozen screenshot, its scale, and where it sits (logical).
struct Frozen {
    QImage image;
    qreal dpr = 1;
    QRectF canvas;
};
Frozen frozen()
{
    auto doc = SpectacleCore::instance()->annotationDocument();
    return {doc->baseImage(), doc->imageDpr() > 0 ? doc->imageDpr() : 1.0, doc->canvasRect()};
}
QRect toPixels(const Frozen &f, const QRectF &logical)
{
    const QRectF r = logical.translated(-f.canvas.topLeft());
    return QRectF(r.topLeft() * f.dpr, r.size() * f.dpr).toAlignedRect().intersected(f.image.rect());
}

QString stripBraces(QString id)
{
    id.remove(u'{');
    id.remove(u'}');
    return id;
}
}

static CaptureTools *s_instance = nullptr;

CaptureTools *CaptureTools::instance()
{
    if (!s_instance) {
        s_instance = new CaptureTools(qApp);
    }
    return s_instance;
}

CaptureTools::CaptureTools(QObject *parent)
    : QObject(parent)
{
}

QString CaptureTools::qrText() const
{
    return m_qrText;
}
QRectF CaptureTools::qrRect() const
{
    return m_qrRect;
}
bool CaptureTools::qrIsLink() const
{
    static const QRegularExpression link(u"^(https?|ftp)://\\S+$"_s, QRegularExpression::CaseInsensitiveOption);
    return link.match(m_qrText.trimmed()).hasMatch();
}

bool CaptureTools::privateInfoReadFor(const QRectF &logicalFrame) const
{
    // a hair of room for the frame's fractions
    return !m_hiding && m_readPicture == m_frozenPicture && m_readFrame.adjusted(-0.5, -0.5, 0.5, 0.5).contains(logicalFrame.normalized());
}

bool CaptureTools::privateInfoReaderRan() const
{
    return m_readRan;
}

bool CaptureTools::hidingPrivateInfo() const
{
    return m_hiding;
}
QString CaptureTools::hideResult() const
{
    return m_hideResult;
}

int CaptureTools::privateInfoFound() const
{
    return m_blurFound;
}

void CaptureTools::takeOffPrivateInfoBlurs()
{
    if (m_hiding) {
        // a reading still going: its blurs are dropped when it ends (the generation moves on)
        ++m_pictureGeneration;
        m_hiding = false;
        m_hideResult.clear();
        Q_EMIT hidingPrivateInfoChanged();
        return;
    }
    auto doc = SpectacleCore::instance()->annotationDocument();
    const int steps = m_blurDepthAfter - m_blurDepthBefore;
    // only when nothing was drawn after them: the blurs are then the last steps to undo
    if (m_blursOff || steps <= 0 || doc->undoStackDepth() != m_blurDepthAfter) {
        return;
    }
    for (int i = 0; i < steps; ++i) {
        doc->undo();
    }
    m_blursOff = true;
    Q_EMIT hidingPrivateInfoChanged();
}

void CaptureTools::putBackPrivateInfoBlurs(const QRectF &logicalFrame)
{
    auto doc = SpectacleCore::instance()->annotationDocument();
    const int steps = m_blurDepthAfter - m_blurDepthBefore;
    if (m_blursOff && steps > 0 && doc->undoStackDepth() == m_blurDepthBefore && doc->redoStackDepth() >= steps) {
        for (int i = 0; i < steps; ++i) {
            doc->redo();
        }
        m_blursOff = false;
        Q_EMIT hidingPrivateInfoChanged();
        return;
    }
    if (m_blursOff || !privateInfoReadFor(logicalFrame) || m_blurFound < 0) {
        m_blursOff = false;
        hidePrivateInfo(logicalFrame);
    }
}
bool CaptureTools::resizing() const
{
    return m_resizing;
}
QString CaptureTools::sizeProblem() const
{
    return m_sizeProblem;
}
QSize CaptureTools::originalSize() const
{
    if (m_resizedUuid.isEmpty()) {
        return {};
    }
    const qreal scale = screenScale(m_resizedFrom);
    return (QSizeF(m_resizedFrom.size()) * scale).toSize();
}

void CaptureTools::setSizeProblem(const QString &text)
{
    if (m_sizeProblem != text) {
        m_sizeProblem = text;
        Q_EMIT sizeProblemChanged();
    }
}

// ── The size menu ──

QSize CaptureTools::screenPixelSize(const QRectF &logicalRect) const
{
    const auto screen = screenFor(logicalRect);
    return (QSizeF(screen->geometry().size()) * screen->devicePixelRatio()).toSize();
}

qreal CaptureTools::screenScale(const QRectF &logicalRect) const
{
    return screenFor(logicalRect)->devicePixelRatio();
}

QVariantList CaptureTools::screens() const
{
    QVariantList list;
    for (auto screen : QGuiApplication::screens()) {
        const QSize px = (QSizeF(screen->geometry().size()) * screen->devicePixelRatio()).toSize();
        list << QVariantMap{{u"name"_s, screen->name()},
                            {u"model"_s, screen->model()},
                            {u"width"_s, px.width()},
                            {u"height"_s, px.height()},
                            {u"rect"_s, QRectF(screen->geometry())}};
    }
    return list;
}

void CaptureTools::frameScreen(int index)
{
    const auto screens = QGuiApplication::screens();
    if (index < 0 || index >= screens.size()) {
        return;
    }
    SelectionEditor::instance()->selection()->setRect(QRectF(screens[index]->geometry()));
}

void CaptureTools::runKWinScript(const QString &source)
{
    // KWin loads a script from a file over D-Bus, runs it, and we unload it after.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    auto file = new QTemporaryFile(dir + u"/ectolens-kwin-XXXXXX.js"_s, this);
    if (!file->open()) {
        qCWarning(SPECTACLE_LOG) << "Could not write the KWin script";
        delete file;
        return;
    }
    file->write(source.toUtf8());
    file->flush();
    const QString plugin = u"ectolens-resize-%1-%2"_s.arg(QCoreApplication::applicationPid()).arg(++m_scriptCount);
    QDBusInterface scripting(u"org.kde.KWin"_s, u"/Scripting"_s, u"org.kde.kwin.Scripting"_s);
    const QDBusReply<int> id = scripting.call(u"loadScript"_s, file->fileName(), plugin);
    if (!id.isValid() || id.value() < 0) {
        qCWarning(SPECTACLE_LOG) << "KWin did not load the resize script:" << id.error().message();
        delete file;
        return;
    }
    QDBusInterface script(u"org.kde.KWin"_s, u"/Scripting/Script%1"_s.arg(id.value()), u"org.kde.kwin.Script"_s);
    script.call(u"run"_s);
    QTimer::singleShot(1000, this, [plugin, file] {
        QDBusInterface scripting(u"org.kde.KWin"_s, u"/Scripting"_s, u"org.kde.kwin.Scripting"_s);
        scripting.call(u"unloadScript"_s, plugin);
        delete file;
    });
}

static QString resizeScript(const QString &uuid, const QRect &frame, bool maximize = false)
{
    // workspace.windowList() and a window's internalId and frameGeometry are KWin 6's scripting API
    // (develop.kde.org/docs/plasma/kwin/api). An object literal sets the geometry, as KWin scripts do.
    return uR"JS((function () {
    const id = "%1";
    for (const w of workspace.windowList()) {
        if (w.internalId.toString().replace(/[{}]/g, "") === id) {
            if (w.maximizable && (w.maximizeMode !== undefined) && w.maximizeMode !== 0) {
                w.setMaximize(false, false);
            }
            w.frameGeometry = { x: %2, y: %3, width: %4, height: %5 };
            if (%6) {
                w.setMaximize(true, true);
            }
        }
    }
})();
)JS"_s.arg(stripBraces(uuid))
        .arg(frame.x())
        .arg(frame.y())
        .arg(frame.width())
        .arg(frame.height())
        .arg(maximize ? u"true"_s : u"false"_s);
}

void CaptureTools::setFrameSize(int pixelWidth, int pixelHeight)
{
    auto editor = SelectionEditor::instance();
    auto selection = editor->selection();
    const QRectF frame = selection->normalized();
    if (pixelWidth <= 0 || pixelHeight <= 0 || frame.isEmpty()) {
        return;
    }
    setSizeProblem({});
    m_resizeClock.start();
    auto screen = screenFor(frame);
    const qreal scale = screen->devicePixelRatio();
    const QSize logical(qRound(pixelWidth / scale), qRound(pixelHeight / scale));
    const QRect screenRect = screen->geometry();
    const QString uuid = editor->frameWindowUuid();

    if (!uuid.isEmpty() && Settings::resizeWindowToFit() && KWindowSystem::isPlatformWayland()) {
        const auto window = WindowPicker::instance()->window(uuid);
        if (!window) {
            return;
        }
        if (m_resizedUuid != uuid) {
            if (!m_resizedUuid.isEmpty()) {
                revertResizedWindow();
            }
            m_resizedUuid = uuid;
            m_resizedFrom = window->geometry;
            m_resizedWasMaximized = window->maximized;
        }
        // Grow from the top-left corner, moving back only as far as needed to stay on its screen.
        QRect target(window->geometry.topLeft(), logical);
        if (target.right() > screenRect.right()) {
            target.moveRight(screenRect.right());
        }
        if (target.bottom() > screenRect.bottom()) {
            target.moveBottom(screenRect.bottom());
        }
        target.moveLeft(std::max(target.left(), screenRect.left()));
        target.moveTop(std::max(target.top(), screenRect.top()));
        m_resizing = true;
        Q_EMIT resizingChanged();
        runKWinScript(resizeScript(uuid, target));
        const int generation = ++m_resizeGeneration;
        QTimer::singleShot(120, this, [this, uuid, logical, generation] {
            finishResize(uuid, logical, 12, generation);
        });
        return;
    }

    // An area you drew, or a window without resizing: the frame takes the size,
    // keeping its top-left corner, inside its screen.
    QRectF target(frame.topLeft(), QSizeF(logical));
    if (target.right() > screenRect.x() + screenRect.width()) {
        target.moveRight(screenRect.x() + screenRect.width());
    }
    if (target.bottom() > screenRect.y() + screenRect.height()) {
        target.moveBottom(screenRect.y() + screenRect.height());
    }
    target.moveLeft(std::max<qreal>(target.left(), screenRect.left()));
    target.moveTop(std::max<qreal>(target.top(), screenRect.top()));
    selection->setRect(target);
}

void CaptureTools::finishResize(const QString &uuid, const QSize &wanted, int triesLeft, int generation)
{
    if (generation != m_resizeGeneration) {
        return; // the overlay closed or the window was put back meanwhile
    }
    const auto window = WindowPicker::instance()->window(uuid);
    // within a pixel: fractional scales round
    auto near = [](const QSize &a, const QSize &b) {
        return std::abs(a.width() - b.width()) <= 1 && std::abs(a.height() - b.height()) <= 1;
    };
    const bool done = window && near(window->geometry.size(), wanted);
    if (!done && triesLeft > 0) {
        QTimer::singleShot(80, this, [this, uuid, wanted, triesLeft, generation] {
            finishResize(uuid, wanted, triesLeft - 1, generation);
        });
        return;
    }
    if (window && !near(window->geometry.size(), wanted)) {
        // The app refused the size (a smallest or largest size of its own).
        const qreal scale = screenScale(window->geometry);
        const QSize got = (QSizeF(window->geometry.size()) * scale).toSize();
        const QSize asked = (QSizeF(wanted) * scale).toSize();
        const QString name = WindowPicker::appName(*window);
        setSizeProblem(i18nc("@info %1 app name, %2×%3 the size asked, %4×%5 the size it took",
                             "%1 can't be %2×%3. It stopped at %4×%5.",
                             name.isEmpty() ? i18nc("@info", "This window") : name,
                             QString::number(asked.width()),
                             QString::number(asked.height()),
                             QString::number(got.width()),
                             QString::number(got.height())));
    }
    if (window) {
        TestScript::report(u"ECTOLENS-RESIZED %1 %2"_s.arg(window->geometry.width()).arg(window->geometry.height()));
    }
    // Take the picture again at the new size, then frame the window where it is now.
    SpectacleCore::instance()->retakeFrozenPicture(uuid);
    m_resizing = false;
    Q_EMIT resizingChanged();
}

void CaptureTools::revertResizedWindow()
{
    ++m_resizeGeneration; // any resize still settling is no longer wanted
    if (m_resizing) {
        m_resizing = false;
        Q_EMIT resizingChanged();
    }
    if (m_resizedUuid.isEmpty()) {
        return;
    }
    if (Settings::revertSizeAfterward()) {
        runKWinScript(resizeScript(m_resizedUuid, m_resizedFrom, m_resizedWasMaximized));
        TestScript::report(u"ECTOLENS-REVERTED %1 %2"_s.arg(m_resizedFrom.width()).arg(m_resizedFrom.height()));
    }
    m_resizedUuid.clear();
    m_resizedFrom = {};
    setSizeProblem({});
    Q_EMIT resizingChanged();
}

// ── Pick Color ──

QColor CaptureTools::colorAt(const QPointF &logicalGlobal) const
{
    const Frozen f = frozen();
    if (f.image.isNull()) {
        return {};
    }
    const QPointF p = (logicalGlobal - f.canvas.topLeft()) * f.dpr;
    const QPoint px(std::clamp(int(p.x()), 0, f.image.width() - 1), std::clamp(int(p.y()), 0, f.image.height() - 1));
    return f.image.pixelColor(px);
}

void CaptureTools::copyText(const QString &text) const
{
    auto data = new QMimeData;
    data->setText(text);
    KSystemClipboard::instance()->setMimeData(data, QClipboard::Clipboard);
    TestScript::report(u"ECTOLENS-CLIPBOARD text "_s + text);
}

// ── Open QR Code ──

void CaptureTools::scanFrozenPicture()
{
    setOverlayMode({});
    ++m_pictureGeneration;
    ++m_frozenPicture;
    // a new picture: no reading of it yet, so no blurs of it to take off or put back
    m_blurFound = -1;
    m_blurDepthBefore = m_blurDepthAfter = -1;
    m_blursOff = false;
    m_qrText.clear();
    m_qrRect = {};
    Q_EMIT qrChanged();
    const Frozen f = frozen();
    if (f.image.isNull()) {
        return;
    }
    auto watcher = new QFutureWatcher<Prison::ScanResult>(this);
    connect(watcher, &QFutureWatcher<Prison::ScanResult>::finished, this, [this, watcher, f] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (!result.hasContent()) {
            return;
        }
        m_qrText = result.hasText() ? result.text() : QString::fromUtf8(result.binaryData());
        const QRectF r = QRectF(result.boundingRect());
        m_qrRect = QRectF(r.topLeft() / f.dpr, r.size() / f.dpr).translated(f.canvas.topLeft());
        Q_EMIT qrChanged();
    });
    const QImage image = f.image;
    watcher->setFuture(QtConcurrent::run([image] {
        return Prison::ImageScanner::scan(image, Prison::Format::QRCode);
    }));
}

void CaptureTools::openQr() const
{
    if (qrIsLink()) {
        auto job = new KIO::OpenUrlJob(QUrl(m_qrText.trimmed()));
        job->start();
    } else {
        copyQr();
    }
}

void CaptureTools::copyQr() const
{
    copyText(m_qrText);
}

// ── Hide Private Info ──

bool CaptureTools::looksPrivate(const QString &word)
{
    return PrivateInfo::looksPrivate(word);
}

QString CaptureTools::readText(const QImage &image, bool hidePrivate)
{
    QString languages = Settings::ocrLanguages().join(u'+');
    if (languages.isEmpty()) {
        languages = u"eng"_s;
    }
    // screen text reads best at about twice its size
    QImage picture = image.convertToFormat(QImage::Format_RGB888);
    if (image.devicePixelRatio() < 1.5) {
        picture = picture.scaled(picture.size() * 2, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    tesseract::TessBaseAPI api;
    if (api.Init(nullptr, languages.toUtf8().constData()) != 0 && api.Init(nullptr, "eng") != 0) {
        return {};
    }
    api.SetPageSegMode(tesseract::PSM_AUTO);
    api.SetImage(picture.constBits(), picture.width(), picture.height(), 3, int(picture.bytesPerLine()));
    api.SetSourceResolution(144);
    std::unique_ptr<char[]> text(api.GetUTF8Text());
    QString words = text ? QString::fromUtf8(text.get()).trimmed() : QString();
    if (hidePrivate) {
        QStringList lines = words.split(u'\n');
        for (auto &line : lines) {
            QStringList parts = line.split(u' ');
            for (auto &part : parts) {
                if (looksPrivate(part)) {
                    part = u"[hidden]"_s;
                }
            }
            line = parts.join(u' ');
        }
        words = lines.join(u'\n');
    }
    return words;
}

void CaptureTools::hidePrivateInfo(const QRectF &logicalFrame)
{
    if (m_hiding) {
        return;
    }
    const Frozen f = frozen();
    const QRect px = toPixels(f, logicalFrame.isEmpty() ? f.canvas : logicalFrame);
    if (f.image.isNull() || px.isEmpty()) {
        return;
    }
    // Upscale small text: Tesseract reads screen text best at about 2x.
    const qreal upscale = f.dpr < 1.5 ? 2.0 : 1.0;
    QImage crop = f.image.copy(px).convertToFormat(QImage::Format_RGB888);
    if (upscale != 1.0) {
        crop = crop.scaled(crop.size() * upscale, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    QString languages = Settings::ocrLanguages().join(u'+');
    if (languages.isEmpty()) {
        languages = u"eng"_s;
    }
    const QRectF asked = logicalFrame.isEmpty() ? QRectF(f.canvas) : logicalFrame.normalized();
    m_hiding = true;
    m_hideResult.clear();
    Q_EMIT hidingPrivateInfoChanged();
    TestScript::report(u"ECTOLENS-HIDING %1 %2 %3 %4"_s.arg(qRound(asked.x())).arg(qRound(asked.y())).arg(qRound(asked.width())).arg(qRound(asked.height())));

    // a later picture (a new capture, a resize) makes this reading stale: its blurs are dropped
    const int generation = ++m_pictureGeneration;
    const int picture = m_frozenPicture;
    using Found = std::optional<QList<QRect>>; // empty when the text reader could not start
    auto watcher = new QFutureWatcher<Found>(this);
    connect(watcher, &QFutureWatcher<Found>::finished, this, [this, watcher, f, px, upscale, generation, picture, asked] {
        const Found result = watcher->result();
        watcher->deleteLater();
        if (generation != m_pictureGeneration) {
            m_hiding = false;
            Q_EMIT hidingPrivateInfoChanged();
            return;
        }
        m_readPicture = picture;
        m_readFrame = asked;
        m_readRan = bool(result);
        if (!result) {
            m_blurFound = -1;
            m_blurDepthBefore = m_blurDepthAfter = -1;
            m_hideResult = i18nc("@info", "Couldn't read the frame: the text reader did not start.");
            m_hiding = false;
            Q_EMIT hidingPrivateInfoChanged();
            return;
        }
        const QList<QRect> found = *result;
        auto doc = SpectacleCore::instance()->annotationDocument();
        m_blurDepthBefore = doc->undoStackDepth();
        auto tool = doc->tool();
        const auto previous = tool->type();
        tool->setType(AnnotationTool::BlurTool);
        for (const QRect &box : found) {
            // back to logical coordinates, with a little room around the word
            const QRectF inPicture(QPointF(px.topLeft()) + QPointF(box.topLeft()) / upscale, QSizeF(box.size()) / upscale);
            QRectF logical(inPicture.topLeft() / f.dpr + f.canvas.topLeft(), inPicture.size() / f.dpr);
            logical.adjust(-3, -3, 3, 3);
            doc->beginItem(logical.topLeft());
            doc->continueItem(logical.bottomRight());
            doc->finishItem();
        }
        doc->deselectItem();
        tool->setType(previous);
        m_blurDepthAfter = doc->undoStackDepth();
        m_blurFound = found.size();
        m_blursOff = false;
        m_hideResult = found.isEmpty() ? i18nc("@info", "Found nothing private in the frame.")
                                       : i18ncp("@info", "Blurred %1 private item.", "Blurred %1 private items.", found.size());
        m_hiding = false;
        Q_EMIT hidingPrivateInfoChanged();
    });
    watcher->setFuture(QtConcurrent::run([crop, languages]() -> Found {
        return PrivateInfo::findBoxes(crop, languages);
    }));
}

// ── Pin ──

void CaptureTools::pin(const QRectF &logicalFrame)
{
    auto doc = SpectacleCore::instance()->annotationDocument();
    const Frozen f = frozen();
    const QRectF frame = logicalFrame.isEmpty() ? f.canvas : logicalFrame;
    QImage image = doc->renderToImage();
    const QRect px = toPixels({image, f.dpr, f.canvas}, frame);
    image = image.copy(px);
    image.setDevicePixelRatio(f.dpr);
    PinWindow::pin(image, frame);
    TestScript::report(u"ECTOLENS-PINNED %1 %2"_s.arg(image.width()).arg(image.height()));
    SpectacleCore::instance()->closeOverlayQuietly();
}

// ── Open with and Share ──

QVariantList CaptureTools::openWithApps() const
{
    QVariantList list;
    list << QVariantMap{{u"id"_s, QString()}, {u"name"_s, i18nc("@item the Ectolens window", "Ectolens")}, {u"icon"_s, u"ectolens"_s}};
    const KService::List services = KApplicationTrader::queryByMimeType(u"image/png"_s);
    for (const auto &service : services) {
        if (service->desktopEntryName() == u"com.ectonomic.ectolens"_s || service->noDisplay()) {
            continue;
        }
        list << QVariantMap{{u"id"_s, service->storageId()}, {u"name"_s, service->name()}, {u"icon"_s, service->icon()}};
    }
    return list;
}

QString CaptureTools::appName(const QString &storageId) const
{
    if (storageId.isEmpty()) {
        return i18nc("@item the Ectolens window", "Ectolens");
    }
    const auto service = KService::serviceByStorageId(storageId);
    return service ? service->name() : storageId;
}

void CaptureTools::openWith(const QString &storageId, const QUrl &url) const
{
    if (!url.isValid()) {
        return;
    }
    const auto service = KService::serviceByStorageId(storageId);
    const QString mime = QMimeDatabase().mimeTypeForUrl(url).name();
    if (!service || !service->hasMimeType(mime)) {
        // the app is gone, or it opens pictures and this is a recording: the default app for it
        auto job = new KIO::OpenUrlJob(url);
        job->start();
        return;
    }
    auto job = new KIO::ApplicationLauncherJob(service);
    job->setUrls({url});
    job->start();
}

#ifdef PURPOSE_FOUND
static QJsonObject shareInput(const QUrl &url)
{
    return QJsonObject{{u"mimeType"_s, QMimeDatabase().mimeTypeForUrl(url).name()}, {u"urls"_s, QJsonArray{url.toString()}}};
}
#endif

QVariantList CaptureTools::shareTargets() const
{
    QVariantList list;
#ifdef PURPOSE_FOUND
    Purpose::AlternativesModel model;
    model.setInputData(shareInput(QUrl::fromLocalFile(QDir::tempPath() + u"/ectolens-share.png"_s)));
    model.setPluginType(u"Export"_s);
    for (int row = 0; row < model.rowCount(); ++row) {
        const auto index = model.index(row);
        // Copy to clipboard is its own row in After Capture and its own button in the window:
        // KDE's share service for it would say the same thing twice
        if (model.data(index, Purpose::AlternativesModel::PluginIdRole).toString() == u"clipboardplugin"_s) {
            continue;
        }
        list << QVariantMap{{u"id"_s, model.data(index, Purpose::AlternativesModel::PluginIdRole)},
                            {u"name"_s, model.data(index, Purpose::AlternativesModel::ActionDisplayRole)},
                            {u"icon"_s, model.data(index, Purpose::AlternativesModel::IconNameRole)}};
    }
#endif
    return list;
}

QString CaptureTools::shareTargetName(const QString &pluginId) const
{
    if (pluginId.isEmpty()) {
        return i18nc("@item share service", "Ask each time");
    }
    for (const auto &v : shareTargets()) {
        const auto m = v.toMap();
        if (m.value(u"id"_s).toString() == pluginId) {
            return m.value(u"name"_s).toString();
        }
    }
    return pluginId;
}

void CaptureTools::share(const QString &pluginId, const QUrl &url)
{
#ifdef PURPOSE_FOUND
    // Purpose's own menu runs the share and asks for anything the service needs
    // (a device for KDE Connect, an account), so trigger its entry for this service.
    auto menu = new Purpose::Menu;
    menu->model()->setInputData(shareInput(url));
    menu->model()->setPluginType(u"Export"_s);
    menu->reload();
    QAction *chosen = nullptr;
    for (auto action : menu->actions()) {
        const int row = action->property("row").toInt();
        const auto index = menu->model()->index(row);
        if (menu->model()->data(index, Purpose::AlternativesModel::PluginIdRole).toString() == pluginId) {
            chosen = action;
            break;
        }
    }
    if (!chosen) {
        delete menu;
        return;
    }
    // Ectolens keeps running until the share is done (an upload can outlast the notification)
    auto keepRunning = std::make_shared<QEventLoopLocker>();
    connect(menu, &Purpose::Menu::finished, menu, [menu, keepRunning](const QJsonObject &output, int error, const QString &message) {
        menu->deleteLater();
        if (error != 0 && error != 1 /* cancelled */) {
            SpectacleCore::instance()->showErrorMessage(i18nc("@info", "Could not share the capture: %1", message));
            return;
        }
        const QString link = output.value(u"url"_s).toString();
        if (!link.isEmpty()) {
            CaptureTools::instance()->copyText(link);
        }
    });
    chosen->trigger();
#else
    Q_UNUSED(pluginId)
    Q_UNUSED(url)
#endif
}

void CaptureTools::showInFolder(const QUrl &url) const
{
    if (url.isLocalFile()) {
        KIO::highlightInFileManager({url});
    }
}

qint64 CaptureTools::msSinceResize() const
{
    return m_resizeClock.isValid() ? m_resizeClock.elapsed() : -1;
}

QString CaptureTools::overlayMode() const
{
    return m_overlayMode;
}

void CaptureTools::setOverlayMode(const QString &mode)
{
    if (m_overlayMode != mode) {
        m_overlayMode = mode;
        TestScript::report(u"ECTOLENS-MODE "_s + (mode.isEmpty() ? u"none"_s : mode));
        Q_EMIT overlayModeChanged();
    }
}

QUrl CaptureTools::savedUrl() const
{
    return m_savedUrl;
}

void CaptureTools::setSavedUrl(const QUrl &url)
{
    if (m_savedUrl != url) {
        m_savedUrl = url;
        Q_EMIT savedUrlChanged();
    }
}

static QUrl currentFile()
{
    auto tools = CaptureTools::instance();
    if (SpectacleCore::instance()->videoMode()) {
        return SpectacleCore::instance()->currentVideo();
    }
    if (tools->savedUrl().isValid()) {
        return tools->savedUrl();
    }
    SpectacleCore::instance()->syncExportImage();
    return ExportManager::instance()->tempSave();
}

void CaptureTools::openCurrentWith(const QString &storageId)
{
    openWith(storageId, currentFile());
}

void CaptureTools::shareCurrent(const QString &pluginId)
{
    share(pluginId, currentFile());
}

void CaptureTools::copyCurrentPath() const
{
    const QUrl url = SpectacleCore::instance()->videoMode() ? SpectacleCore::instance()->currentVideo() : m_savedUrl;
    if (url.isLocalFile()) {
        copyText(url.toLocalFile());
    }
}

// ── The Record panel ──

QVariantList CaptureTools::frameRates() const
{
    int screenMax = 60;
    for (const QScreen *screen : QGuiApplication::screens()) {
        screenMax = std::max(screenMax, int(std::round(screen->refreshRate())));
    }
    QList<int> rates = {30, 60, 120};
    if (screenMax > 120) {
        rates << screenMax;
    }
    const int rate = int(Settings::videoFramerate());
    if (!rates.contains(rate)) {
        rates << rate;
        std::sort(rates.begin(), rates.end());
    }
    QVariantList list;
    for (int r : std::as_const(rates)) {
        list << QVariantMap{{u"value"_s, r}, {u"text"_s, QString::number(r)}};
    }
    return list;
}

QString CaptureTools::qualityNote(int quality) const
{
    const QString chosen = Settings::videoEncoder();
    const QString encoder = RecordingQuality::encoderToUse(chosen);
    const QHash<QString, QString> names = {{u"libx264"_s, u"x264"_s}, {u"libx265"_s, u"x265"_s}, {u"libsvtav1"_s, u"SVT-AV1"_s}, {u"h264_nvenc"_s, u"NVENC"_s},
                                           {u"hevc_nvenc"_s, u"NVENC"_s}};
    if (encoder.isEmpty()) {
        // the quality choices don't reach the system's own encoder (VA-API or OpenH264)
        return chosen.isEmpty() ? i18nc("@info", "Your system's own H.264 encoder records, at its own quality.")
                                : i18nc("@info %1 encoder", "This computer's FFmpeg has no %1, so your system's own H.264 encoder records, at its own quality.", names.value(chosen, chosen));
    }
    // Sizes were measured with x264 on a 3440×1440 test clip at 60 fps (design/bench).
    if (encoder == u"libx264"_s) {
        switch (quality) {
        case 0:
            return i18nc("@info", "About 10 MB a minute with x264. Small text can soften.");
        case 1:
            return i18nc("@info", "About 27 MB a minute with x264. Text stays sharp, and it plays everywhere.");
        case 2:
            return i18nc("@info", "About 28 MB a minute with x264, in full color. Firefox and some phones can't play it.");
        default:
            return i18nc("@info", "Your own settings, from Recording Settings.");
        }
    }
    const QString name = names.value(encoder, encoder);
    switch (quality) {
    case 0:
        return i18nc("@info %1 encoder", "The smallest files %1 makes at a good picture.", name);
    case 1:
        return i18nc("@info %1 encoder", "Sharp text with %1.", name);
    case 2:
        return i18nc("@info %1 encoder", "The best picture %1 makes.", name);
    default:
        return i18nc("@info", "Your own settings, from Recording Settings.");
    }
}

QVariantList CaptureTools::videoFormats() const
{
    QVariantList list;
    const auto supported = SpectacleCore::instance()->videoPlatform()->supportedFormats();
    for (auto f : {VideoPlatform::MP4_H264, VideoPlatform::WebM_VP9, VideoPlatform::Gif, VideoPlatform::WebP}) {
        if (supported.testFlag(f)) {
            list << QVariantMap{{u"value"_s, int(f)}, {u"text"_s, VideoPlatform::extensionForFormat(f).toUpper()}};
        }
    }
    return list;
}

bool CaptureTools::formatHasSound(int format) const
{
    return VideoPlatform::formatSupportsAudio(VideoPlatform::Format(format));
}

// ── Keys ──

QString CaptureTools::globalShortcut(const QString &which) const
{
    auto actions = ShortcutActions::self();
    QAction *action = which == u"open"_s ? actions->openAction()
        : which == u"fullScreen"_s       ? actions->fullScreenAction()
        : which == u"activeWindow"_s     ? actions->activeWindowAction()
        : which == u"region"_s           ? actions->regionAction()
                                         : nullptr;
    if (!action) {
        return {};
    }
    const auto keys = KGlobalAccel::self()->shortcut(action);
    return keys.isEmpty() ? QString() : keys.first().toString(QKeySequence::NativeText);
}

QString CaptureTools::keyText(const QString &portable) const
{
    return QKeySequence::fromString(portable, QKeySequence::PortableText).toString(QKeySequence::NativeText);
}

// ── Settings and help ──

void CaptureTools::openSettings(const QString &page) const
{
    auto dialog = qobject_cast<SettingsDialog *>(KConfigDialog::exists(u"settings"_s));
    if (!dialog) {
        dialog = new SettingsDialog;
        dialog->setAttribute(Qt::WA_DeleteOnClose);
    }
    if (page == u"recording"_s) {
        dialog->showRecordingPage();
    } else if (page == u"appearance"_s) {
        dialog->showAppearancePage();
    } else if (page == u"shortcuts"_s) {
        dialog->showShortcutsPage();
    }
    dialog->show();
    if (dialog->windowHandle()) {
        dialog->windowHandle()->requestActivate();
    }
}

void CaptureTools::openShortcuts() const
{
    openSettings(u"shortcuts"_s);
}

void CaptureTools::help(const QString &which) const
{
    auto menu = HelpMenu::instance();
    if (!menu) {
        return;
    }
    if (which == u"help"_s) {
        menu->showAppHelp();
    } else if (which == u"bug"_s) {
        menu->trigger(KHelpMenu::menuReportBug);
    } else if (which == u"about"_s) {
        menu->trigger(KHelpMenu::menuAboutApp);
    }
}

#include "moc_CaptureTools.cpp"
