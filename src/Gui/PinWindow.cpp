/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#include "PinWindow.h"

#include "Config.h"

#include <KSystemClipboard>
#include <KWindowSystem>
#include <LayerShellQt/Window>

#include <QDir>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMimeData>
#include <QQmlContext>
#include <QScreen>
#include <QStandardPaths>
#include <QTemporaryFile>

using namespace Qt::StringLiterals;

static int s_pins = 0;
static bool s_quitAfterLast = false;

int PinWindow::count()
{
    return s_pins;
}

void PinWindow::quitAfterLast()
{
    s_quitAfterLast = true;
}

PinWindow *PinWindow::pin(const QImage &image, const QRectF &where)
{
    // the room around the picture holds its shadow, so the window is see-through there
    QQuickWindow::setDefaultAlphaBuffer(true);
    auto window = new PinWindow(image, where);
    window->show();
    return window;
}

PinWindow::PinWindow(const QImage &image, const QRectF &where)
    : m_image(image)
    , m_topLeft(where.topLeft())
    , m_keepRunning(std::make_unique<QEventLoopLocker>())
{
    ++s_pins;
    setFlag(Qt::FramelessWindowHint);
    setColor(Qt::transparent);
    setResizeMode(QQuickView::SizeRootObjectToView);
    setTitle(u"Ectolens Pin"_s);
    // the picture as a file the QML Image reads
    QTemporaryFile file(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + u"/ectolens-pin-XXXXXX.png"_s);
    file.setAutoRemove(false);
    if (file.open()) {
        m_image.save(&file, "PNG");
        m_file = file.fileName();
    }
    for (auto screen : QGuiApplication::screens()) {
        if (screen->geometry().contains(where.center().toPoint())) {
            m_screenAt = screen;
        }
    }
    if (!m_screenAt) {
        m_screenAt = QGuiApplication::primaryScreen();
    }
    setScreen(m_screenAt);
    // room around the picture for its shadow (matches PinView.qml's margin)
    const QSizeF size = imageSize() + QSizeF(32, 32);
    resize(size.toSize());
    if (KWindowSystem::isPlatformWayland()) {
        // LayerShellQt only takes a window with no native surface yet (as in CaptureWindow).
        if (handle()) {
            destroy();
        }
        if (auto layer = LayerShellQt::Window::get(this)) {
            using namespace LayerShellQt;
            // KWin turns a layer surface's scope into its window type: a utility window
            // that floats and never animates like a normal window.
            layer->setScope(u"utility"_s);
            layer->setLayer(Window::LayerTop);
            layer->setAnchors(Window::Anchors::fromInt(Window::AnchorTop | Window::AnchorLeft));
            layer->setExclusiveZone(-1);
            layer->setKeyboardInteractivity(Window::KeyboardInteractivityOnDemand);
            layer->setScreen(m_screenAt);
        }
    } else {
        setFlag(Qt::WindowStaysOnTopHint);
    }
    place();
    rootContext()->setContextProperty(u"pinWindow"_s, this);
    setSource(QUrl(u"%1/Gui/PinView.qml"_s.arg(SPECTACLE_QML_PATH)));
}

PinWindow::~PinWindow()
{
    if (--s_pins == 0 && s_quitAfterLast) {
        QMetaObject::invokeMethod(qApp, &QCoreApplication::quit, Qt::QueuedConnection);
    }
    if (!m_file.isEmpty()) {
        QFile::remove(m_file);
    }
}

QUrl PinWindow::imageUrl() const
{
    return QUrl::fromLocalFile(m_file);
}

QSizeF PinWindow::imageSize() const
{
    return QSizeF(m_image.size()) / (m_image.devicePixelRatio() > 0 ? m_image.devicePixelRatio() : 1.0);
}

void PinWindow::place()
{
    const QPoint local = (m_topLeft - QPointF(m_screenAt->geometry().topLeft()) - QPointF(16, 16)).toPoint();
    if (auto layer = LayerShellQt::Window::get(this); layer && KWindowSystem::isPlatformWayland()) {
        layer->setMargins(QMargins(local.x(), local.y(), 0, 0));
        // A layer surface's margins wait for its next commit (wlr-layer-shell: double-buffered),
        // and LayerShellQt only sends them: draw a frame so the move shows while dragging.
        update();
    } else {
        setPosition(m_screenAt->geometry().topLeft() + local);
    }
}

void PinWindow::moveBy(qreal dx, qreal dy)
{
    m_topLeft += QPointF(dx, dy);
    const QRectF screen = m_screenAt->geometry();
    m_topLeft.setX(std::clamp(m_topLeft.x(), screen.left() - imageSize().width() + 40, screen.right() - 40));
    m_topLeft.setY(std::clamp(m_topLeft.y(), screen.top(), screen.bottom() - 40));
    place();
}

void PinWindow::copy() const
{
    auto data = new QMimeData;
    data->setImageData(m_image);
    KSystemClipboard::instance()->setMimeData(data, QClipboard::Clipboard);
}

void PinWindow::unpin()
{
    close();
    deleteLater();
}

void PinWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        unpin();
        return;
    }
    if (event->matches(QKeySequence::Copy)) {
        copy();
        return;
    }
    QQuickView::keyPressEvent(event);
}

#include "moc_PinWindow.cpp"
