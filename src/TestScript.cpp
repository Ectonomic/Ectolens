/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#include "TestScript.h"

#include "ExportManager.h"
#include "Gui/CaptureTools.h"
#include "Gui/CaptureWindow.h"
#include "Gui/FrameHistory.h"
#include "Gui/InlineMessageModel.h"
#include "Gui/ViewerWindow.h"
#include "Gui/Selection.h"
#include "Gui/SelectionEditor.h"
#include "Platforms/VideoPlatform.h"
#include "SpectacleCore.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QMetaEnum>
#include <QPainterPath>
#include <QFile>
#include <QQuickItem>
#include <QQuickView>
#include <QQmlEngine>
#include <QHash>
#include <algorithm>
#include <QScreen>
#include <QTimer>
#include <cstdio>
#include <functional>

using namespace Qt::StringLiterals;

namespace
{
QStringList s_steps;

void say(const QString &line)
{
    printf("%s\n", qPrintable(line));
    fflush(stdout);
}

void next();

// an item by its objectName anywhere in a window's visual tree, open menus included
// (a popup's items hang under the window's overlay layer, not under the root object)
QQuickItem *findVisual(QQuickItem *item, const QString &name)
{
    if (!item) {
        return nullptr;
    }
    if (item->objectName() == name && item->isVisible()) {
        return item;
    }
    const auto children = item->childItems();
    for (auto child : children) {
        if (auto found = findVisual(child, name)) {
            return found;
        }
    }
    return nullptr;
}

// every showing item whose objectName starts so, in a window's visual tree (a Repeater's items too)
void findAllVisual(QQuickItem *item, const QString &prefix, QList<QQuickItem *> &found)
{
    if (!item || !item->isVisible()) {
        return;
    }
    if (item->objectName().startsWith(prefix)) {
        found << item;
    }
    const auto children = item->childItems();
    for (auto child : children) {
        findAllVisual(child, prefix, found);
    }
}

// the selected Text mark: "ECTOLENS-TEXT box=0|1 X Y W H words=W,H chars=N align=A" (its outside rect,
// the label's box, its words' rect size, in document coordinates, and its Qt::Alignment), or "ECTOLENS-TEXT none"
void sayTextInfo()
{
    auto item = SpectacleCore::instance()->annotationDocument()->selectedItemWrapper();
    if (!item->hasSelection() || !(item->options() & AnnotationTool::TextOption)) {
        say(u"ECTOLENS-TEXT none"_s);
        return;
    }
    const QRectF r = item->textOutsideRect();
    const QRectF w = item->geometryPath().boundingRect();
    say(u"ECTOLENS-TEXT box=%1 %2 %3 %4 %5 words=%6,%7 chars=%8 align=%9"_s.arg(item->isTextBox() ? 1 : 0)
            .arg(qRound(r.x())).arg(qRound(r.y())).arg(qRound(r.width())).arg(qRound(r.height()))
            .arg(qRound(w.width())).arg(qRound(w.height())).arg(item->text().size()).arg(item->textAlignment()));
}

// where every showing item named PREFIX... is: in the Ectolens window's own coordinates as
// "ECTOLENS-WAT NAME X Y" (window), or on the screen as "ECTOLENS-AT NAME X Y" (the overlay)
void sayAll(const QString &prefix, bool window)
{
    QList<QQuickItem *> found;
    if (window) {
        if (auto viewer = ViewerWindow::instance(); viewer && viewer->contentItem()) {
            findAllVisual(viewer->contentItem(), prefix, found);
        }
    } else {
        for (const auto capture : CaptureWindow::instances()) {
            findAllVisual(capture->contentItem(), prefix, found);
        }
    }
    for (auto item : std::as_const(found)) {
        const QPointF center(item->width() / 2, item->height() / 2);
        const QPointF p = window ? item->mapToScene(center) : item->mapToGlobal(center);
        say(u"%1 %2 %3 %4"_s.arg(window ? u"ECTOLENS-WAT"_s : u"ECTOLENS-AT"_s, item->objectName()).arg(qRound(p.x())).arg(qRound(p.y())));
    }
    say(u"%1 %2 end"_s.arg(window ? u"ECTOLENS-WAT"_s : u"ECTOLENS-AT"_s, prefix));
}

// after each real mouse release in an Ectolens window, once the release has done its work: its
// number, the selected Text mark and where the box's handles now are ("ECTOLENS-RELEASE N")
class ReleaseWatch : public QObject
{
public:
    using QObject::QObject;
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::MouseButtonRelease && qobject_cast<QQuickWindow *>(watched)) {
            const bool window = watched == ViewerWindow::instance();
            QTimer::singleShot(250, this, [this, window] {
                say(u"ECTOLENS-RELEASE %1"_s.arg(++m_count));
                sayTextInfo();
                sayAll(u"textBoxHandle"_s, window);
            });
        }
        return false;
    }

private:
    int m_count = 0;
};

void later(int ms)
{
    QTimer::singleShot(ms, qApp, &next);
}

void next()
{
    if (s_steps.isEmpty()) {
        say(u"ECTOLENS-STEP done"_s);
        return;
    }
    const QString step = s_steps.takeFirst().trimmed();
    const QString name = step.section(u':', 0, 0);
    const QString arg = step.section(u':', 1);
    auto core = SpectacleCore::instance();
    if (name == u"frame") {
        const auto n = arg.split(u',');
        SelectionEditor::instance()->selection()->setRect(n.value(0).toDouble(), n.value(1).toDouble(), n.value(2).toDouble(), n.value(3).toDouble());
        later(300);
    } else if (name == u"mark") {
        say(u"ECTOLENS-STEP "_s + arg);
        const QString ack = qEnvironmentVariable("ECTOLENS_TEST_ACK") + u'/' + arg;
        auto poll = new QTimer(qApp);
        QObject::connect(poll, &QTimer::timeout, qApp, [poll, ack] {
            if (QFile::exists(ack)) {
                poll->deleteLater();
                next();
            }
        });
        poll->start(100);
    } else if (name == u"hover") {
        const auto n = arg.split(u',');
        SelectionEditor::instance()->hoverAt(QPointF(n.value(0).toDouble(), n.value(1).toDouble()));
        later(300);
    } else if (name == u"click") {
        const auto editor = SelectionEditor::instance();
        if (editor->takeHovered()) {
            const QRect r = editor->selection()->rectF().toRect();
            say(u"ECTOLENS-TOOK %1 %2 %3 %4 %5"_s.arg(editor->frameIsWindow() ? u"window"_s : u"screen"_s).arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height()));
        } else {
            say(u"ECTOLENS-TOOK nothing"_s);
        }
        later(300);
    } else if (name == u"locate") {
        // print where a named control is, in screen coordinates: "ECTOLENS-AT NAME X Y"
        for (const auto window : CaptureWindow::instances()) {
            if (auto item = findVisual(window->contentItem(), arg)) {
                const QPointF p = item->mapToGlobal(QPointF(item->width() / 2, item->height() / 2));
                say(u"ECTOLENS-AT %1 %2 %3"_s.arg(arg).arg(qRound(p.x())).arg(qRound(p.y())));
                break;
            }
        }
        later(100);
    } else if (name == u"wlocate") {
        // where a named control is in the Ectolens window, in the window's own coordinates:
        // "ECTOLENS-WAT NAME X Y" (the test adds where KWin put the window)
        if (auto window = ViewerWindow::instance(); window && window->rootObject()) {
            auto root = window->rootObject();
            QQuickItem *item = root->objectName() == arg ? root : root->findChild<QQuickItem *>(arg);
            if (item && item->isVisible()) {
                const QPointF p = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
                say(u"ECTOLENS-WAT %1 %2 %3"_s.arg(arg).arg(qRound(p.x())).arg(qRound(p.y())));
            } else {
                say(u"ECTOLENS-WAT %1 none"_s.arg(arg));
            }
        }
        later(100);
    } else if (name == u"wclick") {
        // press a named button in the Ectolens window from inside the app (the menus'
        // look, not input: the overlay's input is checked with real pointer events)
        if (auto window = ViewerWindow::instance(); window && window->rootObject()) {
            auto root = window->rootObject();
            if (auto item = root->findChild<QQuickItem *>(arg)) {
                QMetaObject::invokeMethod(item, "clicked");
                say(u"ECTOLENS-WCLICKED %1"_s.arg(arg));
            } else {
                say(u"ECTOLENS-WCLICKED %1 none"_s.arg(arg));
            }
        }
        later(500);
    } else if (name == u"wescape") {
        // close the window's open menus
        if (auto window = ViewerWindow::instance(); window && window->rootObject()) {
            for (auto o : window->rootObject()->findChildren<QObject *>()) {
                if (o->inherits("QQuickPopup") && o->property("opened").toBool()) {
                    QMetaObject::invokeMethod(o, "close");
                }
            }
        }
        later(300);
    } else if (name == u"wresize") {
        if (auto window = ViewerWindow::instance()) {
            const auto n = arg.split(u',');
            window->resize(n.value(0).toInt(), n.value(1).toInt());
        }
        later(800);
    } else if (name == u"size") {
        const auto n = arg.split(u',');
        CaptureTools::instance()->setFrameSize(n.value(0).toInt(), n.value(1).toInt());
        later(300);
    } else if (name == u"framerect") {
        const QRect r = SelectionEditor::instance()->selection()->rectF().toRect();
        say(u"ECTOLENS-FRAME %1 %2 %3 %4 %5"_s.arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height()).arg(SelectionEditor::instance()->frameIsWindow() ? u"window"_s : u"area"_s));
        later(100);
    } else if (name == u"framehistory") {
        // the overlay's undo history: "ECTOLENS-FRAMEHISTORY FRAME_UNDO FRAME_REDO CAN_UNDO CAN_REDO MARKS_UNDO MARKS_REDO"
        auto h = FrameHistory::instance();
        auto doc = SpectacleCore::instance()->annotationDocument();
        say(u"ECTOLENS-FRAMEHISTORY %1 %2 %3 %4 %5 %6"_s.arg(h->undoCount()).arg(h->redoCount()).arg(int(h->canUndo())).arg(int(h->canRedo()))
                .arg(doc->undoStackDepth()).arg(doc->redoStackDepth()));
        later(100);
    } else if (name == u"sliders") {
        // every Ectolens slider showing in the overlay or the window (EctoSlider, found by its type, menus too, and any
        // two-way picker that says its thumb stays inside, thumbInside), on the
        // screen: "ECTOLENS-SLIDER NAME X Y W H PADDING VALUE THUMBX THUMBY THUMBW THUMBH INROW", INROW 1
        // when it stands in a row of controls, whose neighbors it must keep clear of
        QList<QQuickWindow *> windows;
        for (const auto window : CaptureWindow::instances()) {
            windows << window;
        }
        if (auto viewer = ViewerWindow::instance(); viewer && viewer->isVisible()) {
            windows << viewer;
        }
        for (const auto window : std::as_const(windows)) {
            QList<QQuickItem *> all;
            findAllVisual(window->contentItem(), QString(), all);
            for (auto item : std::as_const(all)) {
                if (!QString::fromLatin1(item->metaObject()->className()).startsWith(u"EctoSlider_QMLTYPE"_s)
                    && !item->property("thumbInside").toBool()) {
                    continue;
                }
                auto thumb = findVisual(item, u"sliderThumb"_s);
                bool inRow = false;
                for (auto up = item->parentItem(); up; up = up->parentItem()) {
                    // a row of controls: the overlay's rows, and the window's own (a Row)
                    if (QString::fromLatin1(up->metaObject()->className()).startsWith(u"EctoRow_QMLTYPE"_s) || up->inherits("QQuickRow")) {
                        inRow = true;
                        break;
                    }
                }
                // the overlay's on the screens; the window's in its own coordinates (a Wayland window
                // doesn't know where it is: the test adds where KWin put it)
                const bool viewer = window == ViewerWindow::instance();
                const QPointF p = viewer ? item->mapToScene(QPointF(0, 0)) : item->mapToGlobal(QPointF(0, 0));
                const QPointF tp = !thumb ? QPointF() : viewer ? thumb->mapToScene(QPointF(0, 0)) : thumb->mapToGlobal(QPointF(0, 0));
                say(u"ECTOLENS-SLIDER %1 %2 %3 %4 %5 %6 %7 %8 %9 %10 %11 %12"_s.arg(item->objectName().isEmpty() ? u"-"_s : item->objectName())
                        .arg(p.x()).arg(p.y()).arg(item->width()).arg(item->height())
                        .arg(item->property("padding").toReal()).arg(item->property("value").toReal())
                        .arg(tp.x()).arg(tp.y()).arg(thumb ? thumb->width() : 0).arg(thumb ? thumb->height() : 0).arg(inRow ? 1 : 0));
            }
        }
        later(100);
    } else if (name == u"screens") {
        // every screen, in logical coordinates: "ECTOLENS-SCREEN X Y W H"
        for (const auto screen : qApp->screens()) {
            const QRect g = screen->geometry();
            say(u"ECTOLENS-SCREEN %1 %2 %3 %4"_s.arg(g.x()).arg(g.y()).arg(g.width()).arg(g.height()));
        }
        later(100);
    } else if (name == u"currenttool") {
        // the overlay's button of the tool in use (EctoTools.currentTool), on the screen:
        // "ECTOLENS-CURRENTTOOL NAME X Y W H", or "ECTOLENS-CURRENTTOOL none"
        bool said = false;
        for (const auto window : CaptureWindow::instances()) {
            if (auto tools = findVisual(window->contentItem(), u"overlayTools"_s)) {
                if (auto button = tools->property("currentTool").value<QQuickItem *>()) {
                    const QPointF p = button->mapToGlobal(QPointF(0, 0));
                    say(u"ECTOLENS-CURRENTTOOL %1 %2 %3 %4 %5"_s.arg(button->objectName().isEmpty() ? u"-"_s : button->objectName())
                            .arg(qRound(p.x())).arg(qRound(p.y())).arg(qRound(button->width())).arg(qRound(button->height())));
                    said = true;
                }
                break;
            }
        }
        if (!said) {
            say(u"ECTOLENS-CURRENTTOOL none"_s);
        }
        later(100);
    } else if (name == u"prop") {
        // a property of a named overlay item: "ECTOLENS-PROP NAME PROP VALUE"
        const QString item = arg.section(u',', 0, 0), prop = arg.section(u',', 1);
        for (const auto window : CaptureWindow::instances()) {
            if (auto found = findVisual(window->contentItem(), item)) {
                say(u"ECTOLENS-PROP %1 %2 %3"_s.arg(item, prop, found->property(prop.toLatin1().constData()).toString()));
                break;
            }
        }
        later(100);
    } else if (name == u"framegeometry") {
        // the frame line's item and stroke, and the handles' rect and line, in the overlay's own
        // coordinates: "ECTOLENS-FRAMEGEO X Y W H STROKE HX HY HW HH HLINE RADIUS"
        for (const auto window : CaptureWindow::instances()) {
            auto line = findVisual(window->contentItem(), u"frameLine"_s);
            auto handles = findVisual(window->contentItem(), u"frameHandles"_s);
            if (line && handles) {
                const QRectF l = line->mapRectToScene(QRectF(0, 0, line->width(), line->height()));
                const QRectF h = handles->mapRectToScene(QRectF(0, 0, handles->width(), handles->height()));
                say(u"ECTOLENS-FRAMEGEO %1 %2 %3 %4 %5 %6 %7 %8 %9 %10 %11"_s.arg(l.x()).arg(l.y()).arg(l.width()).arg(l.height())
                        .arg(line->property("strokeWidth").toReal()).arg(h.x()).arg(h.y()).arg(h.width()).arg(h.height())
                        .arg(handles->property("line").toReal()).arg(handles->property("handleRadius").toReal()));
                break;
            }
        }
        later(100);
    } else if (name == u"canvas") {
        auto doc = core->annotationDocument();
        const QSizeF px = doc->canvasRect().size() * doc->imageDpr();
        say(u"ECTOLENS-CANVAS %1 %2"_s.arg(qRound(px.width())).arg(qRound(px.height())));
        later(100);
    } else if (name == u"wprop") {
        // a property of a named item in the Ectolens window: "ECTOLENS-WPROP NAME PROP VALUE"
        const QString item = arg.section(u',', 0, 0), prop = arg.section(u',', 1);
        if (auto window = ViewerWindow::instance(); window && window->rootObject()) {
            if (auto o = window->rootObject()->findChild<QObject *>(item)) {
                say(u"ECTOLENS-WPROP %1 %2 %3"_s.arg(item, prop, o->property(prop.toLatin1().constData()).toString()));
            }
        }
        later(100);
    } else if (name == u"wininfo") {
        for (auto w : CaptureWindow::instances()) {
            say(u"ECTOLENS-CAPWIN visible=%1 active=%2 focusObject=%3"_s.arg(w->isVisible()).arg(w->isActive()).arg(w->focusObject() ? QString::fromLatin1(w->focusObject()->metaObject()->className()) : u"none"_s));
        }
        later(100);
    } else if (name == u"sharetargets") {
        QStringList ids;
        for (const auto &v : CaptureTools::instance()->shareTargets()) {
            ids << v.toMap().value(u"id"_s).toString();
        }
        say(u"ECTOLENS-SHARETARGETS "_s + ids.join(u','));
        later(100);
    } else if (name == u"watchvisible") {
        // for the next MS milliseconds, is every screen's overlay up and drawing? (the size
        // menu's resize must not make it step aside): "ECTOLENS-STAYED-ON yes|no"
        auto timer = new QTimer(qApp);
        auto left = std::make_shared<int>(arg.toInt() / 50);
        auto stayed = std::make_shared<bool>(true);
        QObject::connect(timer, &QTimer::timeout, qApp, [timer, left, stayed] {
            for (auto w : CaptureWindow::instances()) {
                auto root = w->rootObject();
                if (!w->isVisible() || !root || !root->isVisible() || root->opacity() < 1) {
                    *stayed = false;
                }
            }
            if (CaptureWindow::instances().isEmpty()) {
                *stayed = false;
            }
            if (--*left <= 0) {
                timer->deleteLater();
                say(u"ECTOLENS-STAYED-ON "_s + (*stayed ? u"yes"_s : u"no"_s));
                next();
            }
        });
        timer->start(50);
    } else if (name == u"menus") {
        // which of the window's and the overlay's menus are open: "ECTOLENS-MENUS N"
        int open = 0;
        QList<QQuickWindow *> windows;
        for (auto w : CaptureWindow::instances()) {
            windows << w;
        }
        if (ViewerWindow::instance()) {
            windows << ViewerWindow::instance();
        }
        for (auto w : windows) {
            if (auto root = static_cast<QQuickView *>(w)->rootObject()) {
                for (auto o : root->findChildren<QObject *>()) {
                    if (o->inherits("QQuickPopup") && o->property("opened").toBool()) {
                        ++open;
                    }
                }
            }
        }
        say(u"ECTOLENS-MENUS %1"_s.arg(open));
        later(100);
    } else if (name == u"tool") {
        // pick a marking tool by its name in AnnotationTool::Tool, like RectangleTool or CropTool
        auto tool = core->annotationDocument()->tool();
        const auto meta = QMetaEnum::fromType<AnnotationTool::Tool>();
        bool ok = false;
        const int value = meta.keyToValue(arg.toLatin1().constData(), &ok);
        if (ok) {
            tool->setType(AnnotationTool::Tool(value));
        }
        later(300);
    } else if (name == u"draw") {
        // a mark with the tool in use, from one point to another (document coordinates)
        const auto n = arg.split(u',');
        auto doc = core->annotationDocument();
        doc->beginItem(QPointF(n.value(0).toDouble(), n.value(1).toDouble()));
        doc->continueItem(QPointF(n.value(2).toDouble(), n.value(3).toDouble()));
        doc->finishItem();
        later(300);
    } else if (name == u"number" || name == u"text") {
        // a Number at a point, or a Text mark with its words: "number:X,Y", "text:X,Y,WORDS" (document coordinates)
        const auto n = arg.split(u',');
        auto doc = core->annotationDocument();
        doc->tool()->setType(name == u"number" ? AnnotationTool::NumberTool : AnnotationTool::TextTool);
        doc->beginItem(QPointF(n.value(0).toDouble(), n.value(1).toDouble()));
        doc->finishItem();
        if (name == u"text") {
            doc->selectedItemWrapper()->setText(arg.section(u',', 2));
            doc->selectedItemWrapper()->commitChanges();
        }
        later(300);
    } else if (name == u"accept") {
        // Capture, as Enter does
        SelectionEditor::instance()->acceptSelection();
        later(300);
    } else if (name == u"closemenus") {
        // close the overlay's open menus (a key after a closed menu may not arrive: docs/deferred.md, Watch)
        for (auto w : CaptureWindow::instances()) {
            if (auto root = w->rootObject()) {
                for (auto o : root->findChildren<QObject *>()) {
                    if (o->inherits("QQuickPopup") && o->property("opened").toBool()) {
                        QMetaObject::invokeMethod(o, "close");
                    }
                }
            }
        }
        later(300);
    } else if (name == u"rows") {
        // every row showing in the overlay, found by its type (EctoRow), not by a list of names:
        // "ECTOLENS-ROW NAME X Y W H" in screen coordinates
        for (const auto window : CaptureWindow::instances()) {
            std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
                if (!item || !item->isVisible()) {
                    return;
                }
                if (QString::fromLatin1(item->metaObject()->className()).startsWith(u"EctoRow_QMLTYPE"_s)) {
                    const QPointF p = item->mapToGlobal(QPointF(0, 0));
                    say(u"ECTOLENS-ROW %1 %2 %3 %4 %5"_s.arg(item->objectName().isEmpty() ? u"-"_s : item->objectName())
                            .arg(qRound(p.x())).arg(qRound(p.y())).arg(qRound(item->width())).arg(qRound(item->height())));
                }
                const auto children = item->childItems();
                for (auto child : children) {
                    walk(child);
                }
            };
            walk(window->contentItem());
        }
        later(100);
    } else if (name == u"anatomy") {
        // Every row and bar showing, in the overlay and the window, and the parts in it, so a check can hold
        // them to one set of rules (spacing, padding, margin and alignment are equalized and standardized). Found by type, not by a list of names: a row is
        // an EctoRow, a bar any item that says `ectoBar`; the parts are what a person sees as one thing.
        //   "ECTOLENS-ANATOMY ROW WIN ID KIND NAME X Y W H FOLLOWSTOOL"   (KIND row or bar)
        //   "ECTOLENS-ANATOMY PART WIN ID KIND NAME X Y W H INKX INKW GX GW" (INK: where its icon and words are;
        //                                                     G: the group it is in, the row's or bar's own child)
        //   "ECTOLENS-ANATOMY TOOL WIN X Y W H RX RW"                     (the tool in use's button, its tool row)
        //   "ECTOLENS-ANATOMY BEGIN" before each report, "ECTOLENS-ANATOMY WINDOW WIN W H" for the Ectolens window
        // in screen coordinates in the overlay, in the window's own in the window (the test adds where KWin put it).
        static const QStringList partTypes = {u"EctoButton"_s, u"EctoSwatch"_s, u"FrameLabel"_s, u"EctoSlider"_s,
                                              u"EctoLabel"_s, u"EctoKey"_s, u"EctoSeparator"_s};
        static const QStringList containerTypes = {u"EctoSplitButton"_s, u"EctoTools"_s, u"EctoToolOptions"_s};
        // the Ectolens part an item is, through what it is built on (a tool is an EctoButton, the window's
        // Words an EctoSplitButton), else its own type
        auto typeOf = [](QObject *o) {
            QString own;
            for (const QMetaObject *m = o->metaObject(); m; m = m->superClass()) {
                QString c = QString::fromLatin1(m->className());
                const int q = c.indexOf(u"_QMLTYPE"_s);
                const QString name = q > 0 ? c.left(q) : c;
                if (own.isEmpty()) {
                    own = name;
                }
                if (partTypes.contains(name) || containerTypes.contains(name) || name == u"EctoRow"_s) {
                    return name;
                }
            }
            return own;
        };
        QList<QQuickWindow *> windows;
        for (const auto window : CaptureWindow::instances()) {
            windows << window;
        }
        if (auto viewer = ViewerWindow::instance(); viewer && viewer->isVisible()) {
            windows << viewer;
        }
        int nextId = 0;
        say(u"ECTOLENS-ANATOMY BEGIN"_s);
        for (const auto window : std::as_const(windows)) {
            const bool viewer = window == ViewerWindow::instance();
            const QString win = viewer ? u"window"_s : u"overlay"_s;
            if (viewer) {
                say(u"ECTOLENS-ANATOMY WINDOW %1 %2 %3"_s.arg(win).arg(window->width()).arg(window->height()));
            }
            auto at = [viewer](QQuickItem *item) {
                return viewer ? item->mapToScene(QPointF(0, 0)) : item->mapToGlobal(QPointF(0, 0));
            };
            // the ink of a part: its first showing Row inside (a button's icon and words), else the part itself
            std::function<QQuickItem *(QQuickItem *)> firstRow = [&](QQuickItem *item) -> QQuickItem * {
                for (auto child : item->childItems()) {
                    if (!child->isVisible()) {
                        continue;
                    }
                    if (child->inherits("QQuickRow") && child->width() > 0) {
                        return child;
                    }
                    if (auto found = firstRow(child)) {
                        return found;
                    }
                }
                return nullptr;
            };
            std::function<void(QQuickItem *, int, QQuickItem *)> parts = [&](QQuickItem *item, int id, QQuickItem *group) {
                for (auto child : item->childItems()) {
                    if (!child->isVisible()) {
                        continue;
                    }
                    const QString type = typeOf(child);
                    const bool part = partTypes.contains(type);
                    // a group of parts: a positioner (rows, grids, split buttons, the window's own groups) or a
                    // plain item holding some
                    const bool container = containerTypes.contains(type) || child->inherits("QQuickBasePositioner")
                        || (type == u"QQuickItem"_s && !child->childItems().isEmpty() && child->width() > 0);
                    if (!part && container) {
                        parts(child, id, group ? group : child);
                        continue;
                    }
                    // a part, ink with no part around it (words, an icon, a color), or an empty item a positioner
                    // still lays out (a spacer)
                    const bool laidOut = item->inherits("QQuickBasePositioner");
                    if (!part && child->width() <= 0 && !laidOut) {
                        continue;
                    }
                    // a bar's dividing line is not a part, nor a row's own surface or the area taking its clicks
                    if (!part && child->height() < 4 && child->width() > 4 * child->height() && !laidOut) {
                        continue;
                    }
                    if (!part && child->x() == 0 && child->y() == 0 && child->width() == item->width() && child->height() == item->height()) {
                        continue;
                    }
                    const QString kind = part ? type
                        : child->inherits("QQuickText") ? u"Text"_s
                        : (type == u"QQuickItem"_s && child->childItems().isEmpty()) ? u"spacer"_s
                        : type;
                    const QPointF p = at(child);
                    QPointF ink = p;
                    qreal inkWidth = child->width();
                    if (auto text = qobject_cast<QQuickItem *>(child); child->inherits("QQuickText")) {
                        ink.rx() += text->property("leftPadding").toReal();
                        inkWidth = text->property("contentWidth").toReal();
                    } else if (auto row = (type == u"EctoButton"_s || type == u"FrameLabel"_s || type == u"EctoLabel"_s) ? firstRow(child) : nullptr) {
                        ink = at(row);
                        inkWidth = row->width();
                    }
                    QQuickItem *g = group ? group : child;
                    say(u"ECTOLENS-ANATOMY PART %1 %2 %3 %4 %5 %6 %7 %8 %9 %10 %11 %12"_s.arg(win).arg(id).arg(kind)
                            .arg(child->objectName().isEmpty() ? u"-"_s : child->objectName())
                            .arg(p.x(), 0, 'f', 2).arg(p.y(), 0, 'f', 2).arg(child->width(), 0, 'f', 2).arg(child->height(), 0, 'f', 2)
                            .arg(ink.x(), 0, 'f', 2).arg(inkWidth, 0, 'f', 2).arg(at(g).x(), 0, 'f', 2).arg(g->width(), 0, 'f', 2));
                }
            };
            std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
                if (!item || !item->isVisible()) {
                    return;
                }
                const QString type = typeOf(item);
                const bool isRow = type == u"EctoRow"_s;
                const bool isBar = item->property("ectoBar").toBool();
                if (isRow || isBar) {
                    const int id = nextId++;
                    const QPointF p = at(item);
                    say(u"ECTOLENS-ANATOMY ROW %1 %2 %3 %4 %5 %6 %7 %8 %9"_s.arg(win).arg(id).arg(isRow ? u"row"_s : u"bar"_s)
                            .arg(item->objectName().isEmpty() ? u"-"_s : item->objectName())
                            .arg(p.x(), 0, 'f', 2).arg(p.y(), 0, 'f', 2).arg(item->width(), 0, 'f', 2).arg(item->height(), 0, 'f', 2)
                            .arg(item->property("followsTool").toBool() ? 1 : 0));
                    parts(item, id, nullptr);
                }
                if (type == u"EctoTools"_s) {
                    if (auto tool = item->property("currentTool").value<QQuickItem *>()) {
                        const QPointF p = at(tool);
                        const QPointF r = at(item);
                        say(u"ECTOLENS-ANATOMY TOOL %1 %2 %3 %4 %5 %6 %7"_s.arg(win).arg(p.x(), 0, 'f', 2).arg(p.y(), 0, 'f', 2)
                                .arg(tool->width(), 0, 'f', 2).arg(tool->height(), 0, 'f', 2).arg(r.x(), 0, 'f', 2).arg(item->width(), 0, 'f', 2));
                    }
                }
                const auto children = item->childItems();
                for (auto child : children) {
                    walk(child);
                }
            };
            walk(window->contentItem());

            // Every open menu (an EctoMenu, by what it is built on), its lines in order, and in each line its
            // surface (a Rectangle across the line: an item's hover, a field, a well, a separator's line) and its
            // marks (words, icons, arrows, boxes, a control's own surface), so a check can hold the menus to the
            // same standard as the rows:
            //   "ECTOLENS-ANATOMY MENU WIN ID TYPE NAME X Y W H RADIUS"
            //   "ECTOLENS-ANATOMY LINE WIN ID LINE KIND NAME X Y W H"
            //   "ECTOLENS-ANATOMY SURFACE WIN ID LINE X Y W H RADIUS"
            //   "ECTOLENS-ANATOMY MARK WIN ID LINE KIND NAME X Y W H ALIGN" (words: where their ink is; ALIGN their
            //                                                       horizontal alignment, 2 when set right)
            //   "ECTOLENS-ANATOMY CHOICES WIN ID X Y W H RADIUS FIRSTX LASTRIGHT TOP BOTTOM INNERRADIUS"
            //                                                      (a segmented control and where its choices are)
            // Popups' items hang under the window's overlay layer; the popups themselves are found among the
            // window's objects.
            auto isA = [](QObject *o, const QString &type) {
                for (const QMetaObject *m = o->metaObject(); m; m = m->superClass()) {
                    const QString c = QString::fromLatin1(m->className());
                    const int q = c.indexOf(u"_QMLTYPE"_s);
                    if ((q > 0 ? c.left(q) : c) == type) {
                        return true;
                    }
                }
                return false;
            };
            auto ownType = [](QObject *o) {
                const QString c = QString::fromLatin1(o->metaObject()->className());
                const int q = c.indexOf(u"_QMLTYPE"_s);
                return q > 0 ? c.left(q) : c;
            };
            auto named = [](QObject *o) {
                return o->objectName().isEmpty() ? u"-"_s : o->objectName();
            };
            // where words' ink is in their item: past the padding, as their alignment sets them
            auto wordsInk = [](QQuickItem *t) {
                const qreal lp = t->property("leftPadding").toReal(), rp = t->property("rightPadding").toReal();
                const qreal tp = t->property("topPadding").toReal(), bp = t->property("bottomPadding").toReal();
                const qreal cw = t->property("contentWidth").toReal(), ch = t->property("contentHeight").toReal();
                const int h = t->property("horizontalAlignment").toInt(), v = t->property("verticalAlignment").toInt();
                const qreal roomX = t->width() - lp - rp, roomY = t->height() - tp - bp;
                const qreal x = lp + ((h & Qt::AlignRight) ? roomX - cw : (h & Qt::AlignHCenter) ? (roomX - cw) / 2 : 0);
                const qreal y = tp + ((v & Qt::AlignBottom) ? roomY - ch : (v & Qt::AlignVCenter) ? (roomY - ch) / 2 : 0);
                return QRectF(x, y, cw, ch);
            };
            QList<QObject *> objects;
            if (auto view = qobject_cast<QQuickView *>(window); view && view->rootObject()) {
                objects = view->rootObject()->findChildren<QObject *>();
            }
            for (auto o : std::as_const(objects)) {
                if (!o->inherits("QQuickPopup") || !o->property("opened").toBool() || !isA(o, u"EctoMenu"_s)) {
                    continue;
                }
                auto content = o->property("contentItem").value<QQuickItem *>();
                auto box = content ? content->parentItem() : nullptr;
                if (!box || !box->isVisible()) {
                    continue;
                }
                const int id = nextId++;
                const QPointF mp = at(box);
                say(u"ECTOLENS-ANATOMY MENU %1 %2 %3 %4 %5 %6 %7 %8 %9"_s.arg(win).arg(id).arg(ownType(o), named(o))
                        .arg(mp.x(), 0, 'f', 2).arg(mp.y(), 0, 'f', 2).arg(box->width(), 0, 'f', 2).arg(box->height(), 0, 'f', 2)
                        .arg(o->property("radius").toReal(), 0, 'f', 2));
                int lineId = 0;
                // a line's surface is drawn across a line that spans the menu inside its padding
                const qreal inside = box->width() - o->property("leftPadding").toReal() - o->property("rightPadding").toReal();
                auto across = [inside](QQuickItem *c, QQuickItem *lineItem) {
                    return lineItem->width() >= inside - 1 && c->width() >= lineItem->width() - 1;
                };
                static const QStringList lineTypes = {u"EctoMenuItem"_s, u"EctoMenuRow"_s, u"EctoMenuHeader"_s, u"EctoMenuSeparator"_s, u"EctoDelay"_s};
                auto isLine = [&](QQuickItem *item) {
                    for (const auto &type : lineTypes) {
                        if (isA(item, type)) {
                            return true;
                        }
                    }
                    return false;
                };
                // an item's rect where the report measures (a turned arrow's box as it stands on the screen)
                auto rectOf = [&](QQuickItem *c, const QRectF &inItem) {
                    return c->mapRectToScene(inItem).translated(at(c) - c->mapToScene(QPointF(0, 0)));
                };
                QRectF clipRect; // what a clipping item lets show (the opacity bar's checks)
                std::function<void(QQuickItem *, QQuickItem *, int)> marks = [&](QQuickItem *item, QQuickItem *lineItem, int lid) {
                    auto mark = [&](QQuickItem *c, const QString &kind, QRectF inItem, int align, qreal trimRight = 0) {
                        QRectF r = rectOf(c, inItem).adjusted(0, 0, -trimRight, 0);
                        if (!clipRect.isNull()) {
                            r &= clipRect;
                            if (r.isEmpty()) {
                                return;
                            }
                        }
                        say(u"ECTOLENS-ANATOMY MARK %1 %2 %3 %4 %5 %6 %7 %8 %9 %10"_s.arg(win).arg(id).arg(lid).arg(kind, named(c))
                                .arg(r.x(), 0, 'f', 2).arg(r.y(), 0, 'f', 2).arg(r.width(), 0, 'f', 2).arg(r.height(), 0, 'f', 2)
                                .arg(align));
                    };
                    const QList<QQuickItem *> children = item == lineItem && item->inherits("QQuickText") ? QList<QQuickItem *>{item} : item->childItems();
                    for (auto c : children) {
                        if (!c->isVisible() || c->opacity() <= 0 || c->width() <= 0 || c->height() <= 0) {
                            continue;
                        }
                        if (isA(c, u"EctoSegmented"_s)) {
                            // its choices: how far they stand inside it
                            QRectF first, last;
                            qreal inner = 0;
                            for (auto row : c->childItems()) {
                                if (!row->inherits("QQuickRow")) {
                                    continue;
                                }
                                for (auto seg : row->childItems()) {
                                    if (!seg->isVisible() || !seg->inherits("QQuickRectangle")) {
                                        continue;
                                    }
                                    const QRectF r(seg->mapToItem(c, QPointF(0, 0)), seg->size());
                                    if (first.isNull() || r.x() < first.x()) {
                                        first = r;
                                    }
                                    if (last.isNull() || r.right() > last.right()) {
                                        last = r;
                                    }
                                    inner = seg->property("radius").toReal();
                                }
                            }
                            const QPointF p = at(c);
                            say(u"ECTOLENS-ANATOMY CHOICES %1 %2 %3 %4 %5 %6 %7 %8 %9 %10 %11 %12"_s.arg(win).arg(id)
                                    .arg(p.x(), 0, 'f', 2).arg(p.y(), 0, 'f', 2).arg(c->width(), 0, 'f', 2).arg(c->height(), 0, 'f', 2)
                                    .arg(c->property("radius").toReal(), 0, 'f', 2).arg(first.x(), 0, 'f', 2).arg(last.right(), 0, 'f', 2)
                                    .arg(first.y(), 0, 'f', 2).arg(first.bottom(), 0, 'f', 2).arg(inner, 0, 'f', 2));
                            if (across(c, lineItem)) {
                                const QPointF sp = at(c);
                                say(u"ECTOLENS-ANATOMY SURFACE %1 %2 %3 %4 %5 %6 %7 %8"_s.arg(win).arg(id).arg(lid)
                                        .arg(sp.x(), 0, 'f', 2).arg(sp.y(), 0, 'f', 2).arg(c->width(), 0, 'f', 2).arg(c->height(), 0, 'f', 2)
                                        .arg(c->property("radius").toReal(), 0, 'f', 2));
                            } else {
                                mark(c, u"Choices"_s, QRectF(QPointF(0, 0), c->size()), 0);
                            }
                            // the words of its choices, which must stay inside it
                            marks(c, lineItem, lid);
                            continue;
                        }
                        if (c->inherits("QQuickText") || c->inherits("QQuickTextInput")) {
                            const QRectF ink = wordsInk(c);
                            if (ink.width() > 0) {
                                mark(c, c->inherits("QQuickText") ? u"Text"_s : u"Input"_s, ink, c->property("horizontalAlignment").toInt());
                            }
                            if (c != item) {
                                marks(c, lineItem, lid); // a field's placeholder
                            }
                            continue;
                        }
                        if (QString::fromLatin1(c->metaObject()->className()).contains(u"Icon"_s)) {
                            mark(c, u"Icon"_s, QRectF(QPointF(0, 0), c->size()), 0);
                            continue;
                        }
                        if (c->inherits("QQuickShape")) {
                            // an arrow ends where its stroke does (EctoChevron.endRoom, as it stands on the screen)
                            mark(c, u"Shape"_s, QRectF(QPointF(0, 0), c->size()), 0, c->property("endRoom").toReal());
                            continue;
                        }
                        if (c->inherits("QQuickRectangle")) {
                            const QColor fill = c->property("color").value<QColor>();
                            QObject *border = c->property("border").value<QObject *>();
                            const bool drawn = fill.alpha() > 0 || (border && border->property("width").toReal() > 0);
                            // a ring drawn around its control (the chosen swatch's) is not a part of the line
                            if (fill.alpha() == 0 && c->parentItem() && c->width() > c->parentItem()->width() && c->height() > c->parentItem()->height()) {
                                continue;
                            }
                            if (across(c, lineItem)) {
                                const QPointF sp = at(c);
                                say(u"ECTOLENS-ANATOMY SURFACE %1 %2 %3 %4 %5 %6 %7 %8"_s.arg(win).arg(id).arg(lid)
                                        .arg(sp.x(), 0, 'f', 2).arg(sp.y(), 0, 'f', 2).arg(c->width(), 0, 'f', 2).arg(c->height(), 0, 'f', 2)
                                        .arg(c->property("radius").toReal(), 0, 'f', 2));
                            } else if (drawn) {
                                mark(c, u"Rect"_s, QRectF(QPointF(0, 0), c->size()), 0);
                            }
                            marks(c, lineItem, lid);
                            continue;
                        }
                        // a drawing made of bars and boxes alone (the align menu's pictures) is one mark, as a
                        // person sees it
                        bool drawing = !c->childItems().isEmpty();
                        int bars = 0;
                        for (auto part : c->childItems()) {
                            if (!part->isVisible() || part->width() <= 0 || part->height() <= 0) {
                                continue;
                            }
                            bool bare = part->inherits("QQuickRectangle");
                            for (auto inner : part->childItems()) {
                                bare = bare && !inner->isVisible();
                            }
                            drawing = drawing && bare;
                            ++bars;
                        }
                        if (drawing && bars > 0) {
                            mark(c, u"Picture"_s, QRectF(QPointF(0, 0), c->size()), 0);
                            continue;
                        }
                        if (c->clip()) {
                            const QRectF before = clipRect;
                            const QRectF own = rectOf(c, QRectF(QPointF(0, 0), c->size()));
                            clipRect = before.isNull() ? own : (before & own);
                            marks(c, lineItem, lid);
                            clipRect = before;
                            continue;
                        }
                        marks(c, lineItem, lid);
                    }
                };
                std::function<void(QQuickItem *)> line = [&](QQuickItem *item) {
                    if (!item->isVisible() || item->width() <= 0 || item->height() <= 0) {
                        return;
                    }
                    if (!isLine(item)) {
                        if (item->inherits("QQuickListView")) {
                            // its lines in view, top to bottom
                            auto inside = item->property("contentItem").value<QQuickItem *>();
                            QList<QQuickItem *> shown;
                            const QRectF view(at(item), item->size());
                            for (auto d : inside ? inside->childItems() : QList<QQuickItem *>()) {
                                if (d->isVisible() && QRectF(at(d), d->size()).intersects(view)) {
                                    shown << d;
                                }
                            }
                            std::sort(shown.begin(), shown.end(), [](QQuickItem *a, QQuickItem *b) {
                                return a->y() < b->y();
                            });
                            for (auto d : std::as_const(shown)) {
                                line(d);
                            }
                            return;
                        }
                        // an item holding menu lines (a list's line, Record's last line): those
                        bool holds = false;
                        for (auto c : item->childItems()) {
                            holds = holds || (c->isVisible() && isLine(c));
                        }
                        if (holds) {
                            for (auto c : item->childItems()) {
                                line(c);
                            }
                            return;
                        }
                    }
                    const int lid = lineId++;
                    const QPointF p = at(item);
                    say(u"ECTOLENS-ANATOMY LINE %1 %2 %3 %4 %5 %6 %7 %8 %9"_s.arg(win).arg(id).arg(lid).arg(ownType(item), named(item))
                            .arg(p.x(), 0, 'f', 2).arg(p.y(), 0, 'f', 2).arg(item->width(), 0, 'f', 2).arg(item->height(), 0, 'f', 2));
                    marks(item, item, lid);
                };
                for (auto child : content->childItems()) {
                    line(child);
                }
            }

            // The words everywhere in the overlay and the window, menus and tooltips too, and the face they
            // are drawn in; the picture's own marks are the person's and are left out (AnnotationViewport):
            //   "ECTOLENS-ANATOMY FONT WIN FAMILY OWNFACE IN NAME"   (IN: tip for a tooltip, menu, or -)
            //   "ECTOLENS-ANATOMY LOOK SANS MONO"                    (the look's two faces, EctoTheme's)
            QHash<QQuickItem *, QString> popupKinds;
            for (auto o : std::as_const(objects)) {
                if (o->inherits("QQuickPopup") && o->property("visible").toBool()) {
                    if (auto content = o->property("contentItem").value<QQuickItem *>(); content && content->parentItem()) {
                        popupKinds.insert(content->parentItem(), o->inherits("QQuickToolTip") ? u"tip"_s : u"menu"_s);
                    }
                }
            }
            std::function<void(QQuickItem *, const QString &)> fonts = [&](QQuickItem *item, const QString &in) {
                if (!item || !item->isVisible() || item->opacity() <= 0 || item->inherits("AnnotationViewport")) {
                    return;
                }
                const QString here = popupKinds.value(item, in);
                if ((item->inherits("QQuickText") || item->inherits("QQuickTextInput") || item->inherits("QQuickTextEdit"))
                    && !item->property("text").toString().isEmpty()) {
                    const QFont font = item->property("font").value<QFont>();
                    say(u"ECTOLENS-ANATOMY FONT %1 %2 %3 %4 %5"_s.arg(win, QString(font.family()).remove(u' '))
                            .arg(item->property("ownFace").toBool() ? 1 : 0).arg(here, named(item)));
                }
                for (auto child : item->childItems()) {
                    fonts(child, here);
                }
            };
            fonts(window->contentItem(), u"-"_s);
            if (auto view = qobject_cast<QQuickView *>(window); view && view->rootObject()) {
                if (auto engine = qmlEngine(view->rootObject())) {
                    if (auto theme = engine->singletonInstance<QObject *>("org.kde.spectacle.private", "EctoTheme")) {
                        say(u"ECTOLENS-ANATOMY LOOK %1 %2"_s.arg(theme->property("sans").toString().remove(u' '), theme->property("mono").toString().remove(u' ')));
                    }
                }
            }
        }
        later(100);
    } else if (name == u"rect") {
        // where a named item is, in screen coordinates: "ECTOLENS-RECT NAME X Y W H"
        for (const auto window : CaptureWindow::instances()) {
            if (auto item = findVisual(window->contentItem(), arg)) {
                const QPointF p = item->mapToGlobal(QPointF(0, 0));
                say(u"ECTOLENS-RECT %1 %2 %3 %4 %5"_s.arg(arg).arg(qRound(p.x())).arg(qRound(p.y())).arg(qRound(item->width())).arg(qRound(item->height())));
                break;
            }
        }
        later(100);
    } else if (name == u"toolinfo") {
        // the marking tool's settings: "ECTOLENS-TOOL TYPE fill=#RRGGBB/ALPHA words=#RRGGBB family=NAME size=PT align=A"
        auto tool = core->annotationDocument()->tool();
        say(u"ECTOLENS-TOOL %1 fill=%2/%3 words=%4 family=%5 size=%6 align=%7"_s.arg(QString::fromLatin1(QMetaEnum::fromType<AnnotationTool::Tool>().valueToKey(tool->type())))
                .arg(tool->fillColor().name())
                .arg(tool->fillColor().alpha())
                .arg(tool->fontColor().name())
                .arg(QString(tool->font().family()).remove(u' '))
                .arg(tool->font().pointSize())
                .arg(tool->textAlignment()));
        later(100);
    } else if (name == u"textinfo") {
        sayTextInfo();
        later(100);
    } else if (name == u"locateall" || name == u"wlocateall") {
        // every showing control whose name starts so (the box's handles): its place, then "PREFIX end"
        sayAll(arg, name == u"wlocateall");
        later(100);
    } else if (name == u"watchreleases") {
        qApp->installEventFilter(new ReleaseWatch(qApp));
        later(100);
    } else if (name == u"wzoom") {
        // the Ectolens window's zoom and where the picture shows in the window, in its own coordinates
        // (the part inside the scrolling view): "ECTOLENS-ZOOM Z X Y W H"
        if (auto viewer = ViewerWindow::instance(); viewer && viewer->contentItem()) {
            QList<QQuickItem *> all;
            findAllVisual(viewer->contentItem(), QString(), all);
            for (auto item : std::as_const(all)) {
                if (!item->inherits("AnnotationViewport")) {
                    continue;
                }
                QRectF shown = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
                for (auto up = item->parentItem(); up; up = up->parentItem()) {
                    if (up->inherits("QQuickFlickable")) {
                        shown &= up->mapRectToScene(QRectF(0, 0, up->width(), up->height()));
                        break;
                    }
                }
                say(u"ECTOLENS-ZOOM %1 %2 %3 %4 %5"_s.arg(item->scale()).arg(qRound(shown.x())).arg(qRound(shown.y())).arg(qRound(shown.width())).arg(qRound(shown.height())));
                break;
            }
        }
        later(100);
    } else if (name == u"deselect") {
        core->annotationDocument()->deselectItem();
        later(200);
    } else if (name == u"save") {
        ExportManager::instance()->exportImage(ExportManager::Save | ExportManager::UserAction);
        later(800);
    } else if (name == u"message") {
        // a message strip of a kind: error, scanned
        if (arg == u"error") {
            InlineMessageModel::instance()->push(InlineMessageModel::Error, u"Couldn't save to /mnt/backup: the folder is read-only"_s);
        } else if (arg == u"scanned") {
            InlineMessageModel::instance()->push(InlineMessageModel::Scanned, u"Found a QR code: https://ectonomic.com"_s, u"https://ectonomic.com"_s);
        }
        later(200);
    } else if (name == u"record") {
        core->recordSelection();
        later(300);
    } else if (name == u"pause") {
        core->videoPlatform()->pauseRecording();
        later(100);
    } else if (name == u"resume") {
        core->videoPlatform()->resumeRecording();
        later(100);
    } else if (name == u"stop") {
        core->finishRecording();
        later(100);
    } else if (name == u"wait") {
        later(arg.toInt());
    } else {
        say(u"ECTOLENS-STEP unknown "_s + step);
        later(0);
    }
}
}

void TestScript::startIfRequested()
{
    const QString script = qEnvironmentVariable("ECTOLENS_TEST_SCRIPT");
    if (script.isEmpty()) {
        return;
    }
    s_steps = script.split(u';', Qt::SkipEmptyParts);
    QObject::connect(SpectacleCore::instance()->videoPlatform(), &VideoPlatform::recordingSaved, qApp, [](const QUrl &url) {
        say(u"ECTOLENS-SAVED "_s + url.toLocalFile());
    });
    QObject::connect(SpectacleCore::instance()->videoPlatform(), &VideoPlatform::recordingFailed, qApp, [](const QString &message) {
        say(u"ECTOLENS-FAILED "_s + message);
    });
    QObject::connect(SpectacleCore::instance(), &SpectacleCore::shareTargetNeeded, qApp, [] {
        say(u"ECTOLENS-SHARE-ASKED"_s);
    });
    QObject::connect(CaptureTools::instance(), &CaptureTools::hidingPrivateInfoChanged, qApp, [] {
        if (!CaptureTools::instance()->hidingPrivateInfo()) {
            say(u"ECTOLENS-HIDDEN "_s + CaptureTools::instance()->hideResult());
        }
    });
    // Start once the overlay is up.
    auto wait = new QTimer(qApp);
    QObject::connect(wait, &QTimer::timeout, qApp, [wait] {
        // the overlay, or the Ectolens window opened without a capture
        if (!CaptureWindow::instances().isEmpty() || (ViewerWindow::instance() && ViewerWindow::instance()->isVisible())) {
            wait->deleteLater();
            later(1000);
        }
    });
    wait->start(100);
}
