/*
 *  SPDX-FileCopyrightText: 2018 Ambareesh "Amby" Balaji <ambareeshbalaji@gmail.com>
 *  SPDX-FileCopyrightText: 2022 Noah Davis <noahadvs@gmail.com>
 *  SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "SelectionEditor.h"
#include "Gui/CaptureTools.h"
#include "SpectacleCore.h"

#include "CaptureWindow.h"
#include "Selection.h"
#include "Geometry.h"
#include "settings.h"
#include "Platforms/WindowPicker.h"
#include "DebugUtils.h"

#include <KLocalizedString>
#include <KWindowSystem>

#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmapCache>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <QtMath>
#include <qnamespace.h>

using namespace Qt::StringLiterals;
using G = Geometry;
using Location = SelectionEditor::Location;

class SelectionEditorSingleton
{
public:
    SelectionEditor self;
};

Q_GLOBAL_STATIC(SelectionEditorSingleton, privateSelectionEditorSelf)

static constexpr qreal s_handleRadiusMouse = 9;
static constexpr qreal s_handleRadiusTouch = 12;
static constexpr qreal s_minSpacingBetweenHandles = 20;
static constexpr qreal s_magnifierLargeStep = 15;
// how long Ctrl is held on its own before the magnifier shows (a shortcut's Ctrl is shorter)
static constexpr int s_ctrlHoldMs = 250;

// Map the global position of the scene to a logical global position (if necessary),
// then translate the scene position of the event to a logical global position using
// the logical global position of the scene. If you try to get the global event position
// and then convert it to a logical global position, you will get the wrong mouse position
// since the event position in the scene is already scaled by the scene's device pixel ratio.
QPointF mapSceneToLogicalGlobalPoint(const QPointF &point, QQuickItem *item)
{
    auto window = item ? item->window() : nullptr;
    Q_ASSERT(window != nullptr);
    return point + G::mapFromPlatformPoint(window->position(), window->devicePixelRatio());
}

// SelectionEditorPrivate =====================
class SelectionEditorPrivate
{
public:
    SelectionEditorPrivate(SelectionEditor *q);

    SelectionEditor *const q;

    void updateHandlePositions();

    qreal dprRound(qreal value) const;

    void handleArrowKey(QKeyEvent *event);
    // Ectolens: the frame follows a drag; Shift keeps its proportions (a new frame: a square)
    void dragTo(Qt::KeyboardModifiers modifiers);
    // Ectolens: the magnifier shows while Ctrl is held on its own, after a moment, so a
    // shortcut such as Ctrl+Z never flashes it; a drag with Ctrl held shows it at once
    void ctrlPressed();
    void ctrlReleased();
    void followCtrl(Qt::KeyboardModifiers modifiers, bool dragging);

    QPointF determineInitialMouseOffset() const;

    void setMouseCursor(QQuickItem *item, const QPointF &pos);
    Location mouseLocation(const QPointF &pos) const;

    void setDragLocation(Location location)
    {
        if (dragLocation == location) {
            return;
        }
        lastDragLocation = dragLocation;
        dragLocation = location;
        Q_EMIT q->dragLocationChanged();
    }

    bool validMagnifierLocation(Location location) const
    {
        return location != Location::None && location != Location::Inside;
    }

    void setShowMagnifier(bool show)
    {
        show = (show || Settings::showMagnifier() == Settings::ShowMagnifierAlways) //
            && Settings::showMagnifier() != Settings::ShowMagnifierNever //
            && validMagnifierLocation(magnifierLocation);
        if (showMagnifier == show) {
            return;
        }
        showMagnifier = show;
        Q_EMIT q->showMagnifierChanged();
    }

    void updateShowMagnifier()
    {
        setShowMagnifier(showMagnifier);
    }

    void setMagnifierLocation(Location location)
    {
        if (magnifierLocation == location) {
            return;
        }
        magnifierLocation = location;
        // if valid, move then show, else hide then move
        if (validMagnifierLocation(magnifierLocation)) {
            Q_EMIT q->magnifierLocationChanged();
            updateShowMagnifier();
        } else {
            setShowMagnifier(false);
            Q_EMIT q->magnifierLocationChanged();
        }
    }

    const std::unique_ptr<Selection> selection;

    QPointF startPos;
    QPointF initialTopLeft;
    QPointF initialMouseOffset;
    QRectF initialRect; // the frame when the drag began
    QPointF initialCorner; // the dragged corner or edge's point when the drag began
    Qt::KeyboardModifiers dragModifiers;
    QTimer ctrlHold;
    bool ctrlHeld = false;
    Location dragLocation = Location::None;
    Location lastDragLocation = Location::None;
    qreal devicePixelRatio = 1;
    qreal devicePixel = 1;
    QPointF mousePos;
    // Ectolens: window and screen picking
    QRectF hoverRect;
    QString hoverUuid;
    QRectF pickedRect;
    QString pickedUuid;
    QString hoverIcon; // the hovered window's app icon
    QString pickedIcon;
    QString pickedApp; // the app's name, for the notification's title
    QString pendingPick; // frame this window once the overlay is back (after a resize)
    void updateHover();
    bool showMagnifier = Settings::showMagnifier() == Settings::ShowMagnifierAlways;
    Location magnifierLocation = Location::FollowMouse;
    bool disableArrowKeys = false;
    QSet<Qt::Key> pressedKeys;
    QRectF screensRect;
    // Midpoints of handles
    QList<QPointF> handlePositions = QList<QPointF>{8};
    QRectF handlesRect;
    // Radius of handles is either handleRadiusMouse or handleRadiusTouch
    qreal handleRadius = s_handleRadiusMouse;
    bool forceReleaseCapture = false;
};

SelectionEditorPrivate::SelectionEditorPrivate(SelectionEditor *q)
    : q(q)
    , selection(new Selection(q))
{
    ctrlHold.setSingleShot(true);
    ctrlHold.setInterval(s_ctrlHoldMs);
    QObject::connect(&ctrlHold, &QTimer::timeout, q, [this] {
        if (ctrlHeld) {
            setShowMagnifier(true);
        }
    });
}

void SelectionEditorPrivate::ctrlPressed()
{
    if (ctrlHeld) {
        return;
    }
    ctrlHeld = true;
    ctrlHold.start();
}

void SelectionEditorPrivate::ctrlReleased()
{
    ctrlHeld = false;
    ctrlHold.stop();
    setShowMagnifier(false);
}

void SelectionEditorPrivate::followCtrl(Qt::KeyboardModifiers modifiers, bool dragging)
{
    // a hover's modifiers can lag the keys, so only a drag's are trusted here
    if (!dragging) {
        return;
    }
    if (!(modifiers & Qt::ControlModifier)) {
        // Ctrl let go where the overlay didn't hear it
        if (ctrlHeld) {
            ctrlReleased();
        }
    } else {
        ctrlHeld = true;
        ctrlHold.stop();
        setShowMagnifier(true);
    }
}

void SelectionEditorPrivate::updateHandlePositions()
{
    // Rectangular region
    const qreal left = selection->left();
    const qreal centerX = selection->horizontalCenter();
    const qreal right = selection->right();
    const qreal top = selection->top();
    const qreal centerY = selection->verticalCenter();
    const qreal bottom = selection->bottom();

    // rectangle too small: make handles free-floating
    qreal offset = 0;
    // rectangle too close to screen edges: move handles on that edge inside the rectangle, so they're still visible
    qreal offsetTop = 0;
    qreal offsetRight = 0;
    qreal offsetBottom = 0;
    qreal offsetLeft = 0;

    const qreal minDragHandleSpace = 4 * handleRadius + 2 * s_minSpacingBetweenHandles;
    const qreal minEdgeLength = qMin(selection->width(), selection->height());
    if (minEdgeLength < minDragHandleSpace) {
        offset = (minDragHandleSpace - minEdgeLength) / 2.0;
    } else {
        const auto translatedScreensRect = screensRect.translated(-screensRect.topLeft());

        offsetTop = top - translatedScreensRect.top() - handleRadius;
        offsetTop = (offsetTop >= 0) ? 0 : offsetTop;

        offsetRight = translatedScreensRect.right() - right - handleRadius + devicePixel;
        offsetRight = (offsetRight >= 0) ? 0 : offsetRight;

        offsetBottom = translatedScreensRect.bottom() - bottom - handleRadius + devicePixel;
        offsetBottom = (offsetBottom >= 0) ? 0 : offsetBottom;

        offsetLeft = left - translatedScreensRect.left() - handleRadius;
        offsetLeft = (offsetLeft >= 0) ? 0 : offsetLeft;
    }

    // top-left handle
    handlePositions[0] = QPointF{left - offset - offsetLeft, top - offset - offsetTop};
    // top-right handle
    handlePositions[1] = QPointF{right + offset + offsetRight, top - offset - offsetTop};
    // bottom-right handle
    handlePositions[2] = QPointF{right + offset + offsetRight, bottom + offset + offsetBottom};
    // bottom-left
    handlePositions[3] = QPointF{left - offset - offsetLeft, bottom + offset + offsetBottom};
    // top-center handle
    handlePositions[4] = QPointF{centerX, top - offset - offsetTop};
    // right-center handle
    handlePositions[5] = QPointF{right + offset + offsetRight, centerY};
    // bottom-center handle
    handlePositions[6] = QPointF{centerX, bottom + offset + offsetBottom};
    // left-center handle
    handlePositions[7] = QPointF{left - offset - offsetLeft, centerY};

    QPointF radiusOffset = {handleRadius, handleRadius};
    QRectF newHandlesRect = {handlePositions[0] - radiusOffset, // top left
                             handlePositions[2] + radiusOffset}; // bottom right
    if (handlesRect == newHandlesRect) {
        return;
    }
    handlesRect = newHandlesRect;
    Q_EMIT q->handlesRectChanged();
}

qreal SelectionEditorPrivate::dprRound(qreal value) const
{
    return G::dprRound(value, devicePixelRatio);
}

void SelectionEditorPrivate::handleArrowKey(QKeyEvent *event)
{
    if (disableArrowKeys) {
        return;
    }

    const auto key = static_cast<Qt::Key>(event->key());
    const auto modifiers = event->modifiers();
    const bool modifySize = modifiers & Qt::AltModifier;
    const qreal step = modifiers & Qt::ShiftModifier ? devicePixel : dprRound(s_magnifierLargeStep);
    QRectF selectionRect = selection->rectF();

    // The current event should override any previously pressed keys
    bool leftArrow = key == Qt::Key_Left || (pressedKeys.contains(Qt::Key_Left) && !pressedKeys.contains(Qt::Key_Right));
    bool rightArrow = key == Qt::Key_Right || (pressedKeys.contains(Qt::Key_Right) && !pressedKeys.contains(Qt::Key_Left));
    bool upArrow = key == Qt::Key_Up || (pressedKeys.contains(Qt::Key_Up) && !pressedKeys.contains(Qt::Key_Down));
    bool downArrow = key == Qt::Key_Down || (pressedKeys.contains(Qt::Key_Down) && !pressedKeys.contains(Qt::Key_Up));

    const bool brMag = modifySize || selection->width() == 0.0 || selection->height() == 0.0;
    auto magLocation = Location::None;

    if (leftArrow) {
        magLocation = Location::Left;
        if (modifySize) {
            selectionRect = G::rectAdjustedVisually(selectionRect, 0, 0, -step, 0);
        } else {
            selectionRect.translate(-step, 0);
        }
    }
    if (rightArrow) {
        magLocation = Location::Right;
        if (modifySize) {
            selectionRect = G::rectAdjustedVisually(selectionRect, 0, 0, step, 0);
        } else {
            selectionRect.translate(step, 0);
        }
    }
    if (upArrow) {
        if (magLocation == Location::Left) {
            magLocation = Location::TopLeft;
        } else if (magLocation == Location::Right) {
            magLocation = Location::TopRight;
        } else {
            magLocation = Location::Top;
        }
        if (modifySize) {
            selectionRect = G::rectAdjustedVisually(selectionRect, 0, 0, 0, -step);
        } else {
            selectionRect.translate(0, -step);
        }
    }
    if (downArrow) {
        if (magLocation == Location::Left) {
            magLocation = Location::BottomLeft;
        } else if (magLocation == Location::Right) {
            magLocation = Location::BottomRight;
        } else {
            magLocation = Location::Bottom;
        }
        if (modifySize) {
            selectionRect = G::rectAdjustedVisually(selectionRect, 0, 0, 0, step);
        } else {
            selectionRect.translate(0, step);
        }
    }
    setMagnifierLocation(brMag ? Location::BottomRight : magLocation);
    selection->setRect(modifySize ? selectionRect : G::rectBounded(selectionRect, screensRect));
}

void SelectionEditorPrivate::dragTo(Qt::KeyboardModifiers modifiers)
{
    const bool keepRatio = modifiers & Qt::ShiftModifier;
    const qreal w0 = qAbs(initialCorner.x() - startPos.x());
    const qreal h0 = qAbs(initialCorner.y() - startPos.y());
    // which way from the fixed corner the dragged one started: a drag straight through keeps a side
    const auto side = [](qreal d, qreal fallback) {
        return d > 0 ? 1.0 : d < 0 ? -1.0 : (fallback >= 0 ? 1.0 : -1.0);
    };
    switch (dragLocation) {
    case Location::TopLeft:
    case Location::TopRight:
    case Location::BottomRight:
    case Location::BottomLeft: {
        auto p = mousePos - initialMouseOffset;
        if (keepRatio && w0 > 0 && h0 > 0) {
            // the larger of the two stretches, both ways
            const qreal dx = p.x() - startPos.x();
            const qreal dy = p.y() - startPos.y();
            const qreal scale = qMax(qAbs(dx) / w0, qAbs(dy) / h0);
            p = QPointF(startPos.x() + dprRound(side(dx, initialCorner.x() - startPos.x()) * w0 * scale),
                        startPos.y() + dprRound(side(dy, initialCorner.y() - startPos.y()) * h0 * scale));
        }
        const bool afterX = p.x() >= startPos.x();
        const bool afterY = p.y() >= startPos.y();
        selection->setRect(afterX ? startPos.x() : p.x(),
                           afterY ? startPos.y() : p.y(),
                           qAbs(p.x() - startPos.x()) + (afterX ? devicePixel : 0),
                           qAbs(p.y() - startPos.y()) + (afterY ? devicePixel : 0));
        break;
    }
    case Location::Outside: {
        auto p = mousePos;
        if (keepRatio) {
            // a new frame drawn with Shift is a square
            const qreal dx = p.x() - startPos.x();
            const qreal dy = p.y() - startPos.y();
            const qreal length = qMax(qAbs(dx), qAbs(dy));
            p = QPointF(startPos.x() + side(dx, 1) * length, startPos.y() + side(dy, 1) * length);
        }
        selection->setRect(qMin(p.x(), startPos.x()),
                           qMin(p.y(), startPos.y()),
                           qAbs(p.x() - startPos.x()) + devicePixel,
                           qAbs(p.y() - startPos.y()) + devicePixel);
        break;
    }
    case Location::Top:
    case Location::Bottom: {
        const auto p = mousePos - initialMouseOffset;
        const bool afterY = p.y() >= startPos.y();
        const qreal height = qAbs(p.y() - startPos.y()) + (afterY ? devicePixel : 0);
        qreal x = initialRect.x();
        qreal width = initialRect.width();
        if (keepRatio && initialRect.width() > 0 && initialRect.height() > 0) {
            // the width follows, growing both ways from the frame's middle
            width = dprRound(height * initialRect.width() / initialRect.height());
            x = dprRound(initialRect.center().x() - width / 2);
        }
        selection->setRect(x, afterY ? startPos.y() : p.y(), width, height);
        break;
    }
    case Location::Right:
    case Location::Left: {
        const auto p = mousePos - initialMouseOffset;
        const bool afterX = p.x() >= startPos.x();
        const qreal width = qAbs(p.x() - startPos.x()) + (afterX ? devicePixel : 0);
        qreal y = initialRect.y();
        qreal height = initialRect.height();
        if (keepRatio && initialRect.width() > 0 && initialRect.height() > 0) {
            height = dprRound(width * initialRect.height() / initialRect.width());
            y = dprRound(initialRect.center().y() - height / 2);
        }
        selection->setRect(afterX ? startPos.x() : p.x(), y, width, height);
        break;
    }
    case Location::Inside: {
        QRectF newRect(mousePos - startPos + initialTopLeft, selection->sizeF());
        selection->setRect(G::rectBounded(newRect, screensRect));
        break;
    }
    default:
        break;
    }
}

// TODO: change cursor with pointerhandlers in qml?
void SelectionEditorPrivate::setMouseCursor(QQuickItem *item, const QPointF &pos)
{
    const auto mouseState = mouseLocation(pos);
    if (mouseState == Location::Outside) {
        item->setCursor(Qt::CrossCursor);
    } else if (mouseState == Location::TopLeft || mouseState == Location::BottomRight) {
        item->setCursor(Qt::SizeFDiagCursor);
    } else if (mouseState == Location::TopRight || mouseState == Location::BottomLeft) {
        item->setCursor(Qt::SizeBDiagCursor);
    } else if (mouseState == Location::Top || mouseState == Location::Bottom) {
        item->setCursor(Qt::SizeVerCursor);
    } else if (mouseState == Location::Left || mouseState == Location::Right) {
        item->setCursor(Qt::SizeHorCursor);
    } else {
        item->setCursor(Qt::OpenHandCursor);
    }
}

SelectionEditor::Location SelectionEditorPrivate::mouseLocation(const QPointF &pos) const
{
    if (selection->isEmpty()) {
        return Location::Outside;
    }

    QRectF handleRect(-handleRadius, -handleRadius, handleRadius * 2, handleRadius * 2);
    if (G::ellipseContains(handleRect.translated(handlePositions[0]), pos)) {
        return Location::TopLeft;
    }
    if (G::ellipseContains(handleRect.translated(handlePositions[1]), pos)) {
        return Location::TopRight;
    }
    if (G::ellipseContains(handleRect.translated(handlePositions[2]), pos)) {
        return Location::BottomRight;
    }
    if (G::ellipseContains(handleRect.translated(handlePositions[3]), pos)) {
        return Location::BottomLeft;
    }
    if (G::ellipseContains(handleRect.translated(handlePositions[4]), pos)) {
        return Location::Top;
    }
    if (G::ellipseContains(handleRect.translated(handlePositions[5]), pos)) {
        return Location::Right;
    }
    if (G::ellipseContains(handleRect.translated(handlePositions[6]), pos)) {
        return Location::Bottom;
    }
    if (G::ellipseContains(handleRect.translated(handlePositions[7]), pos)) {
        return Location::Left;
    }

    const auto rect = selection->normalized();
    // Rectangle can be resized when border is dragged, if it's big enough
    if (rect.width() >= 100 && rect.height() >= 100) {
        if (rect.adjusted(0, -handleRadius, 0, -rect.height() + handleRadius).contains(pos)) {
            return Location::Top;
        }
        if (rect.adjusted(0, rect.height() - handleRadius, 0, handleRadius).contains(pos)) {
            return Location::Bottom;
        }
        if (rect.adjusted(-handleRadius, 0, -rect.width() + handleRadius, 0).contains(pos)) {
            return Location::Left;
        }
        if (rect.adjusted(rect.width() - handleRadius, 0, handleRadius, 0).contains(pos)) {
            return Location::Right;
        }
    }
    if (rect.contains(pos)) {
        return Location::Inside;
    }
    return Location::Outside;
}

QPointF SelectionEditorPrivate::determineInitialMouseOffset() const
{
    switch (dragLocation) {
    case Location::TopLeft:
        return mousePos - selection->rectF().topLeft();
    case Location::TopRight:
        return mousePos - selection->rectF().topRight();
    case Location::BottomRight:
        return mousePos - selection->rectF().bottomRight();
    case Location::BottomLeft:
        return mousePos - selection->rectF().bottomLeft();
    case Location::Top:
        return mousePos - QPointF(0, selection->rectF().top());
    case Location::Bottom:
        return mousePos - QPointF(0, selection->rectF().bottom());
    case Location::Left:
        return mousePos - QPointF(selection->rectF().left(), 0);
    case Location::Right:
        return mousePos - QPointF(selection->rectF().right(), 0);
    default:
        return {};
    }
}

// SelectionEditor =================================

SelectionEditor::SelectionEditor(QObject *parent)
    : QObject(parent)
    , d(new SelectionEditorPrivate(this))
{
    setObjectName(u"selectionEditor"_s);

    connect(d->selection.get(), &Selection::rectChanged, this, [this](){
        d->updateHandlePositions();
        Q_EMIT frameIsWindowChanged(); // a moved or resized frame is no longer the window
        d->updateHover();
    });
}

SelectionEditor *SelectionEditor::instance()
{
    return &privateSelectionEditorSelf()->self;
}

Selection *SelectionEditor::selection() const
{
    return d->selection.get();
}

qreal SelectionEditor::devicePixelRatio() const
{
    return d->devicePixelRatio;
}

QRectF SelectionEditor::screensRect() const
{
    return d->screensRect;
}

qreal SelectionEditor::screensWidth() const
{
    return d->screensRect.width();
}

qreal SelectionEditor::screensHeight() const
{
    return d->screensRect.height();
}

Location SelectionEditor::dragLocation() const
{
    return d->dragLocation;
}

QRectF SelectionEditor::handlesRect() const
{
    return d->handlesRect;
}

QPointF SelectionEditor::mousePosition() const
{
    return d->mousePos;
}

bool SelectionEditor::showMagnifier() const
{
    return d->showMagnifier;
}

Location SelectionEditor::magnifierLocation() const
{
    return d->magnifierLocation;
}

void SelectionEditor::forceReleaseCapture(const bool enabled) const
{
    d->forceReleaseCapture = enabled;
}

bool SelectionEditor::acceptSelection(ExportManager::Actions actions)
{
    if (d->screensRect.isEmpty()) {
        return false;
    }
    // Ectolens: not while Hide Private Info is still reading the frame (the picture would
    // leave before its blurs are on it)
    if (CaptureTools::instance()->hidingPrivateInfo()) {
        return false;
    }

    auto selectionRect = d->selection->normalized();
    if (Settings::rememberSelectionRect() == Settings::Always) {
        Settings::setSelectionRect(selectionRect.toAlignedRect());
    }

    if (selectionRect.isEmpty()) {
        selectionRect = d->screensRect;
    }

    Q_EMIT accepted(selectionRect, actions);
    return true;
}

void SelectionEditor::reset()
{
    const auto windows = CaptureWindow::instances();
    if (windows.empty()) [[unlikely]] {
        Log::warning() << "CaptureWindows were not created before calling SelectionEditor::reset(). This should not happen.";
        return;
    }
    qreal dpr = CaptureWindow::maxDevicePixelRatio();
    if (d->devicePixelRatio != dpr) {
        d->devicePixelRatio = dpr;
        d->devicePixel = 1 / dpr;
        Q_EMIT devicePixelRatioChanged();
    }

    QRectF rect;
    for (auto window : windows) {
        rect |= Geometry::mapFromPlatformRect(window->geometry(), dpr);
    }
    if (d->screensRect != rect) {
        d->screensRect = rect;
        Q_EMIT screensRectChanged();
    }

    auto remember = Settings::rememberSelectionRect();
    if (remember == Settings::Never) {
        d->selection->setRect({});
    } else if (remember == Settings::Always) {
        auto selectionRect = Settings::selectionRect();
        if (selectionRect.width() < 0 || selectionRect.height() < 0) {
            selectionRect = {0, 0, 0, 0};
        }
        d->selection->setRect(selectionRect);
    }
    if (!d->pendingPick.isEmpty()) {
        pickWindow(d->pendingPick);
        d->pendingPick.clear();
    }
}

void SelectionEditor::setPendingPick(const QString &uuid)
{
    d->pendingPick = uuid;
}

bool SelectionEditor::eventFilter(QObject *watched, QEvent *event)
{
    auto *item = qobject_cast<QQuickItem *>(watched);

    if (!item) {
        return false;
    }

    switch (event->type()) {
    case QEvent::KeyPress:
        keyPressEvent(item, static_cast<QKeyEvent *>(event));
        break;
    case QEvent::KeyRelease:
        keyReleaseEvent(item, static_cast<QKeyEvent *>(event));
        break;
    case QEvent::ShortcutOverride:
        // Ectolens: a shortcut with Ctrl (Ctrl+Z, Ctrl+C) may never reach keyPressEvent;
        // it is no hold for the magnifier either
        if (d->ctrlHeld && static_cast<QKeyEvent *>(event)->key() != Qt::Key_Control) {
            d->ctrlHold.stop();
            d->setShowMagnifier(false);
        }
        break;
    case QEvent::HoverMove:
        hoverMoveEvent(item, static_cast<QHoverEvent *>(event));
        break;
    case QEvent::MouseButtonPress:
        mousePressEvent(item, static_cast<QMouseEvent *>(event));
        break;
    case QEvent::MouseMove:
        mouseMoveEvent(item, static_cast<QMouseEvent *>(event));
        break;
    case QEvent::MouseButtonRelease:
        mouseReleaseEvent(item, static_cast<QMouseEvent *>(event));
        break;
    case QEvent::MouseButtonDblClick:
        mouseDoubleClickEvent(item, static_cast<QMouseEvent *>(event));
        break;
    default:
        break;
    }
    return false;
}

void SelectionEditor::keyPressEvent(QQuickItem *item, QKeyEvent *event)
{
    d->pressedKeys.insert(event->keyCombination().key());
    Q_UNUSED(item);
    // Ectolens: while an AI agent's request asks (its dialog up, before Adjust First), the frame
    // stays as its preview shows it: the arrows and Ctrl's magnifier wait, and only Enter (Send)
    // and Esc (Don't Send) answer
    const auto core = SpectacleCore::instance();
    const bool agentDialog = core && core->agentAsking() && !core->agentAdjusting();
    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        acceptSelection();
        event->accept();
        break;
    case Qt::Key_Up:
    case Qt::Key_Right:
    case Qt::Key_Down:
    case Qt::Key_Left:
        if (agentDialog) {
            event->accept();
            break;
        }
        d->handleArrowKey(event);
        d->setShowMagnifier(event->modifiers().testFlag(Qt::ControlModifier));
        event->accept();
        break;
    case Qt::Key_Control:
        if (!agentDialog) {
            d->ctrlPressed();
        }
        break;
    case Qt::Key_Shift:
        // Shift pressed mid-drag keeps the frame's proportions from here
        if (d->dragLocation != Location::None) {
            d->dragTo(d->dragModifiers | Qt::ShiftModifier);
        }
        break;
    default:
        // Ctrl with another key is a shortcut (Ctrl+Z, Ctrl+C): no magnifier
        if (d->ctrlHeld) {
            d->ctrlHold.stop();
            d->setShowMagnifier(false);
        }
        break;
    }
}

void SelectionEditor::keyReleaseEvent(QQuickItem *item, QKeyEvent *event)
{
    d->pressedKeys.remove(event->keyCombination().key());
    Q_UNUSED(item);
    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        event->accept();
        break;
    case Qt::Key_Up:
    case Qt::Key_Right:
    case Qt::Key_Down:
    case Qt::Key_Left:
        event->accept();
        break;
    case Qt::Key_Control:
        d->ctrlReleased();
        break;
    case Qt::Key_Shift:
        if (d->dragLocation != Location::None) {
            d->dragTo(d->dragModifiers & ~Qt::ShiftModifier);
        }
        break;
    default:
        break;
    }
}

void SelectionEditorPrivate::updateHover()
{
    QRectF rect;
    QString uuid;
    QString icon;
    if (selection->isEmpty() && dragLocation == SelectionEditor::None) {
        if (KWindowSystem::isPlatformWayland()) {
            if (const auto window = WindowPicker::instance()->windowAt(mousePos)) {
                rect = QRectF(window->geometry).intersected(screensRect);
                uuid = window->uuid;
                icon = window->iconName;
            }
        }
        if (rect.isEmpty()) {
            // over the desktop: the whole screen under the pointer
            for (const auto window : CaptureWindow::instances()) {
                const QRectF screen = window->screenToFollow()->geometry();
                if (screen.contains(mousePos)) {
                    rect = screen;
                    break;
                }
            }
        }
    }
    if (rect != hoverRect || uuid != hoverUuid || icon != hoverIcon) {
        hoverRect = rect;
        hoverUuid = uuid;
        hoverIcon = icon;
        Q_EMIT q->hoverRectChanged();
    }
}

QRectF SelectionEditor::hoverRect() const
{
    return d->hoverRect;
}

bool SelectionEditor::hoverIsWindow() const
{
    return !d->hoverUuid.isEmpty();
}

bool SelectionEditor::frameIsWindow() const
{
    return !frameWindowUuid().isEmpty();
}

QString SelectionEditor::frameWindowUuid() const
{
    return !d->pickedUuid.isEmpty() && d->selection->rectF() == d->pickedRect ? d->pickedUuid : QString();
}

void SelectionEditor::hoverAt(const QPointF &position)
{
    d->mousePos = position;
    Q_EMIT mousePositionChanged();
    d->updateHover();
}

bool SelectionEditor::takeHovered()
{
    if (d->hoverRect.isEmpty()) {
        return false;
    }
    // Copy first: setting the selection clears the hover.
    const QRectF rect = d->hoverRect;
    const QString uuid = d->hoverUuid;
    const QString icon = d->hoverIcon;
    d->pickedUuid.clear();
    d->selection->setRect(rect);
    d->pickedRect = d->selection->rectF();
    d->pickedUuid = uuid;
    d->pickedIcon = icon;
    d->pickedApp.clear();
    if (const auto window = uuid.isEmpty() || !KWindowSystem::isPlatformWayland() ? std::nullopt : WindowPicker::instance()->window(uuid)) {
        d->pickedApp = WindowPicker::appName(*window);
    }
    Q_EMIT frameIsWindowChanged();
    return true;
}

bool SelectionEditor::pickWindow(const QString &uuid)
{
    if (uuid.isEmpty() || !KWindowSystem::isPlatformWayland()) {
        return false;
    }
    const auto window = WindowPicker::instance()->window(uuid);
    if (!window) {
        return false;
    }
    d->pickedUuid.clear();
    d->selection->setRect(QRectF(window->geometry).intersected(d->screensRect));
    d->pickedRect = d->selection->rectF();
    d->pickedUuid = uuid;
    d->pickedIcon = window->iconName;
    d->pickedApp = WindowPicker::appName(*window);
    Q_EMIT frameIsWindowChanged();
    return true;
}

QString SelectionEditor::hoverIconName() const
{
    return d->hoverIcon;
}

QString SelectionEditor::frameIconName() const
{
    return frameIsWindow() ? d->pickedIcon : QString();
}

QString SelectionEditor::frameAppName() const
{
    return frameIsWindow() ? d->pickedApp : QString();
}

void SelectionEditor::hoverMoveEvent(QQuickItem *item, QHoverEvent *event)
{
    if (!item->window() || !item->window()->screen()) {
        return;
    }
    auto scenePosition = G::dprRound(event->scenePosition(), item->window()->devicePixelRatio());
    d->mousePos = mapSceneToLogicalGlobalPoint(scenePosition, item);
    Q_EMIT mousePositionChanged();
    d->setMouseCursor(item, d->mousePos);
    d->updateHover();
}

void SelectionEditor::mousePressEvent(QQuickItem *item, QMouseEvent *event)
{
    if (!item->window() || !item->window()->screen()) {
        return;
    }

    if (event->source() == Qt::MouseEventNotSynthesized) {
        d->handleRadius = s_handleRadiusMouse;
    } else {
        d->handleRadius = s_handleRadiusTouch;
    }

    if (event->button() & (Qt::LeftButton | Qt::RightButton)) {
        if (event->button() & Qt::RightButton) {
            d->selection->setRect({});
        }
        item->setFocus(true);
        auto scenePosition = G::dprRound(event->scenePosition(), item->window()->devicePixelRatio());
        d->mousePos = mapSceneToLogicalGlobalPoint(scenePosition, item);
        Q_EMIT mousePositionChanged();
        d->setDragLocation(d->mouseLocation(d->mousePos));
        d->setMagnifierLocation(d->dragLocation);
        d->initialMouseOffset = d->determineInitialMouseOffset();
        d->initialRect = d->selection->rectF();
        d->initialCorner = d->mousePos - d->initialMouseOffset;
        d->dragModifiers = event->modifiers();
        d->followCtrl(event->modifiers(), d->dragLocation != Location::Inside && d->dragLocation != Location::None);
        d->disableArrowKeys = true;

        switch (d->dragLocation) {
        case Location::Outside:
            d->startPos = d->mousePos;
            break;
        case Location::Inside:
            d->startPos = d->mousePos;
            d->initialTopLeft = d->selection->rectF().topLeft();
            item->setCursor(Qt::ClosedHandCursor);
            break;
        case Location::Top:
        case Location::Left:
        case Location::TopLeft:
            d->startPos = d->selection->rectF().bottomRight();
            break;
        case Location::Bottom:
        case Location::Right:
        case Location::BottomRight:
            d->startPos = d->selection->rectF().topLeft();
            break;
        case Location::TopRight:
            d->startPos = d->selection->rectF().bottomLeft();
            break;
        case Location::BottomLeft:
            d->startPos = d->selection->rectF().topRight();
            break;
        default:
            break;
        }
    }
    event->accept();
}

void SelectionEditor::mouseMoveEvent(QQuickItem *item, QMouseEvent *event)
{
    if (!item->window() || !item->window()->screen()) {
        return;
    }

    auto scenePosition = G::dprRound(event->scenePosition(), item->window()->devicePixelRatio());
    d->mousePos = mapSceneToLogicalGlobalPoint(scenePosition, item);
    Q_EMIT mousePositionChanged();
    d->setMagnifierLocation(d->dragLocation);
    if (d->dragLocation == Location::None) {
        d->setMouseCursor(item, d->mousePos);
    } else {
        d->dragModifiers = event->modifiers();
        d->followCtrl(event->modifiers(), d->dragLocation != Location::Inside);
        d->dragTo(event->modifiers());
    }

    event->accept();
}

void SelectionEditor::mouseReleaseEvent(QQuickItem *item, QMouseEvent *event)
{
    switch (event->button()) {
    case Qt::LeftButton:
    case Qt::RightButton:
        if (d->dragLocation == Location::Outside && d->selection->width() <= 2 * d->devicePixel && d->selection->height() <= 2 * d->devicePixel
            && !d->hoverRect.isEmpty() && event->button() == Qt::LeftButton) {
            // Ectolens: a click without a drag takes the window under the pointer,
            // or the whole screen over the desktop.
            takeHovered();
            if (Settings::useReleaseToCapture() || d->forceReleaseCapture) {
                acceptSelection();
            }
        } else if (d->dragLocation == Location::Outside && (Settings::useReleaseToCapture() || d->forceReleaseCapture)) {
            acceptSelection();
        } else {
            d->disableArrowKeys = false;
            if (d->dragLocation == Location::Inside) {
                item->setCursor(Qt::OpenHandCursor);
            }
        }
        break;
    default:
        break;
    }
    d->setDragLocation(Location::None);
    d->setMagnifierLocation(Location::FollowMouse);
    d->initialMouseOffset = {};
    event->accept();
}

void SelectionEditor::mouseDoubleClickEvent(QQuickItem *item, QMouseEvent *event)
{
    Q_UNUSED(item)
    if (event->button() == Qt::LeftButton && (d->selection->contains(d->mousePos) || d->selection->isEmpty())) {
        acceptSelection();
    }
    d->setMagnifierLocation(Location::FollowMouse);
    event->accept();
}

#include <moc_SelectionEditor.cpp>
