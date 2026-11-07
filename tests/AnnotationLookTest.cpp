/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 *
 * The Text and Number marks' look (a nicer default for them, in Ecto green), drawn by Ectolens's copy of
 * KQuickImageEditor and read back pixel by pixel:
 * - a Number is an Ecto green badge with near-black digits and a ring one shade darker,
 *   a pill once the number is wider than the circle;
 * - a Text mark is an Ecto green label: its words near-black on a rounded box padded around
 *   them, with the same ring; without its fill it is plain Ecto green words;
 * - both start in Geist Semibold, and the words on any fill take whichever of near-black or
 *   white reads on it.
 * Text boxes (a click and drag draws a text box that constrains the text):
 * - a drag with the Text tool draws a box; its words wrap inside it, long words break, and it
 *   keeps its drawn size, growing down only so no words are hidden; a short drag is a click;
 * - its handles resize it and the words rewrap (undo puts it back); a click's label becomes a box;
 * - the painted lines break where Qt Quick's TextEdit (the editing box, wrapMode Wrap) breaks them.
 * The settings live in a throwaway config (test mode), so the defaults are the defaults.
 */
#include <KQuickImageEditor/AnnotationDocument>
#include <KQuickImageEditor/AnnotationTool>
#include <KQuickImageEditor/AnnotationViewport>
#include "traits.h"

#include <QFile>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QImage>
#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <KSharedConfig>
#include <KLocalizedQmlContext>
#include <KLocalizedString>
#include <QQmlContext>
#include <QtQml/QQmlExtensionPlugin>
#include <ctime>
#include <functional>
#include <tuple>

// Ectolens's copy of KQuickImageEditor's QML (TextTool.qml and the rest), as the app imports it
Q_IMPORT_QML_PLUGIN(KQuickImageEditorPlugin)

using namespace Qt::StringLiterals;

namespace
{
const QColor ectoGreen(200, 242, 46);

bool near(const QColor &a, const QColor &b, int tolerance = 6)
{
    return std::abs(a.red() - b.red()) <= tolerance && std::abs(a.green() - b.green()) <= tolerance && std::abs(a.blue() - b.blue()) <= tolerance;
}

// ECTOLENS_LOOK_SAVE=DIR keeps the drawn pictures, to look at
void save(const QImage &image, const QString &name)
{
    if (const QString dir = qEnvironmentVariable("ECTOLENS_LOOK_SAVE"); !dir.isEmpty()) {
        image.save(dir + u'/' + QString(name).remove(u'#') + u".png"_s);
    }
}

int luma(const QColor &c)
{
    return (c.red() * 299 + c.green() * 587 + c.blue() * 114) / 1000;
}

// the darkest pixel in a rect of an image
QColor darkest(const QImage &image, const QRect &rect)
{
    QColor best(255, 255, 255);
    for (int y = rect.top(); y <= rect.bottom(); ++y) {
        for (int x = rect.left(); x <= rect.right(); ++x) {
            const QColor c = image.pixelColor(x, y);
            if (luma(c) < luma(best)) {
                best = c;
            }
        }
    }
    return best;
}
}

class AnnotationLookTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void defaults();
    void numberBadge();
    void numberPill();
    void textLabel();
    void textWithoutLabel();
    void labelStaysPut();
    void emptyTextDrawsNothing();
    void wordsReadOnAnyFill();
    void shortDragIsAClick();
    void dragDrawsABox();
    void boxGrowsSoWordsShow();
    void boxBreaksLongWords();
    void resizeRewraps();
    void clickLabelBecomesABox();
    void editorBreaksLikePainting();
    void textToolBreaksLikePainting();
    void bigFontKeepsRoomForWords();
    void boxWithoutLabel();
    void boxHandleTakesAReleaseOnlyDrag();
    void handlesStayOffTheWords();
    void editorPlacesWordsLikePainting();
    void alignmentMovesTheWords();
    void labelFitsAnyFontSize();
    void toolAlignmentCarriesOver();
    void verticalAlignmentInABox();
    void labelGrowsFromItsAlignedSide();
    void longWordsPaintToTheEnd();
    void undoFollowsWhatHappenedLast();
    void shiftAndCtrlShapeTheBox();
    void labelPaddingTakesThePress();
    void typingStaysQuickWithManyBoxes();

private:
    std::unique_ptr<AnnotationDocument> document(const QColor &base, const QSize &size = {240, 140});
    // a Text box drawn by a drag from one point to another, with these words, deselected
    SelectedItemWrapper *drawBox(AnnotationDocument *doc, QPointF from, QPointF to, const QString &words);
};

void AnnotationLookTest::initTestCase()
{
    KLocalizedString::setApplicationDomain(QByteArrayLiteral("spectacle"));
    QStandardPaths::setTestModeEnabled(true);
    // a fresh config: the defaults, not anyone's saved choices
    QFile::remove(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + u'/' + QCoreApplication::applicationName() + u"rc"_s);
    KSharedConfig::openConfig()->reparseConfiguration();
    for (const auto &name : {u"Geist-SemiBold.ttf"_s, u"Geist-Regular.ttf"_s, u"Geist-Medium.ttf"_s}) {
        QVERIFY(QFontDatabase::addApplicationFont(QStringLiteral(ECTOLENS_FONTS_DIR) + u'/' + name) >= 0);
    }
}

std::unique_ptr<AnnotationDocument> AnnotationLookTest::document(const QColor &base, const QSize &size)
{
    auto doc = std::make_unique<AnnotationDocument>();
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(base);
    doc->setBaseImage(image);
    return doc;
}

void AnnotationLookTest::defaults()
{
    auto doc = document(Qt::white);
    auto tool = doc->tool();
    tool->setType(AnnotationTool::NumberTool);
    QCOMPARE(tool->fillColor(), ectoGreen);
    QCOMPARE(tool->font().family(), u"Geist"_s);
    QCOMPARE(tool->font().weight(), QFont::DemiBold);
    QVERIFY(tool->hasShadow());
    tool->setType(AnnotationTool::TextTool);
    QVERIFY2(tool->options().testFlag(AnnotationTool::FillOption), "a text mark has a fill, for its label");
    QCOMPARE(tool->fillColor(), ectoGreen);
    QCOMPARE(tool->fontColor(), ectoGreen);
    QCOMPARE(tool->font().family(), u"Geist"_s);
    QCOMPARE(tool->font().weight(), QFont::DemiBold);
}

void AnnotationLookTest::numberBadge()
{
    for (const QColor &base : {QColor(Qt::white), QColor(24, 24, 27)}) {
        auto doc = document(base);
        doc->tool()->setType(AnnotationTool::NumberTool);
        doc->tool()->setShadow(false);
        doc->tool()->setNumber(1);
        doc->beginItem(QPointF(60, 70));
        doc->finishItem();
        doc->deselectItem();
        const QImage image = doc->renderToImage();
        save(image, u"number-"_s + base.name());
        const qreal radius = QFontMetricsF(doc->tool()->font()).capHeight() * 1.33;
        // the fill, between the digit and the edge
        const QColor fill = image.pixelColor(60, qRound(70 + radius * 0.75));
        QVERIFY2(near(fill, ectoGreen), qPrintable(u"badge fill %1"_s.arg(fill.name())));
        // the digit: near-black
        const QColor digit = darkest(image, QRect(54, 64, 13, 13));
        QVERIFY2(luma(digit) < 40, qPrintable(u"digit %1"_s.arg(digit.name())));
        // the ring: darker than the fill, at the badge's edge
        const QColor ring = image.pixelColor(qRound(60 + radius), 70);
        QVERIFY2(luma(ring) < luma(ectoGreen) - 25 && !near(ring, base, 20), qPrintable(u"ring %1 on %2"_s.arg(ring.name(), base.name())));
        // and just outside it, the picture
        QVERIFY(near(image.pixelColor(qRound(60 + radius + 3), 70), base));
    }
}

void AnnotationLookTest::numberPill()
{
    auto doc = document(Qt::white);
    doc->tool()->setType(AnnotationTool::NumberTool);
    doc->tool()->setNumber(1000);
    doc->beginItem(QPointF(120, 70));
    doc->finishItem();
    const QRectF rect = doc->selectedItemWrapper()->geometryPath().boundingRect();
    QVERIFY2(rect.width() > rect.height() * 1.3, qPrintable(u"a 4-digit badge is %1 by %2"_s.arg(rect.width()).arg(rect.height())));
    QVERIFY2(qAbs(rect.center().x() - 120) < 0.5 && qAbs(rect.center().y() - 70) < 0.5, "the pill stays centered where it was placed");

    // a placed 9 changed to 1000 becomes a pill in place, and back to a circle at 7
    doc->tool()->setNumber(9);
    doc->beginItem(QPointF(60, 40));
    doc->finishItem();
    auto item = doc->selectedItemWrapper();
    const QRectF circle = item->geometryPath().boundingRect();
    QVERIFY2(qAbs(circle.width() - circle.height()) < 0.5, "a one-digit badge is a circle");
    item->setNumber(1000);
    const QRectF pill = item->geometryPath().boundingRect();
    QVERIFY2(pill.width() > pill.height() * 1.3 && qAbs(pill.center().x() - 60) < 0.5 && qAbs(pill.center().y() - 40) < 0.5,
             qPrintable(u"1000 after 9: %1 by %2 at %3,%4"_s.arg(pill.width()).arg(pill.height()).arg(pill.center().x()).arg(pill.center().y())));
    item->setNumber(7);
    const QRectF back = item->geometryPath().boundingRect();
    QVERIFY2(qAbs(back.width() - back.height()) < 0.5 && qAbs(back.center().x() - 60) < 0.5, "back to one digit, back to a circle in place");
}

void AnnotationLookTest::textLabel()
{
    for (const QColor &base : {QColor(Qt::white), QColor(24, 24, 27)}) {
        auto doc = document(base);
        doc->tool()->setType(AnnotationTool::TextTool);
        doc->tool()->setShadow(false);
        doc->beginItem(QPointF(40, 60));
        doc->finishItem();
        auto item = doc->selectedItemWrapper();
        item->setText(u"Run this first"_s);
        item->commitChanges();
        const QRectF words = item->geometryPath().boundingRect();
        const QSizeF pad = Traits::textLabelPadding(item->font());
        QVERIFY(pad.width() >= 4 && pad.height() >= 2);
        doc->deselectItem();
        const QImage image = doc->renderToImage();
        save(image, u"label-"_s + base.name());
        // the label's box: Ecto green in the padding left of the words, and above them
        const QColor left = image.pixelColor(qRound(words.left() - pad.width() / 2), qRound(words.center().y()));
        QVERIFY2(near(left, ectoGreen), qPrintable(u"label box %1"_s.arg(left.name())));
        const QColor top = image.pixelColor(qRound(words.center().x()), qRound(words.top() - pad.height() / 2));
        QVERIFY2(near(top, ectoGreen), qPrintable(u"label box above the words %1"_s.arg(top.name())));
        // the words, near-black on the green
        const QColor ink = darkest(image, words.toAlignedRect());
        QVERIFY2(luma(ink) < 40, qPrintable(u"words %1"_s.arg(ink.name())));
        // rounded: the box's very corner is the picture, not green
        const QColor corner = image.pixelColor(qFloor(words.left() - pad.width()), qFloor(words.top() - pad.height()));
        QVERIFY2(!near(corner, ectoGreen, 30), qPrintable(u"corner %1"_s.arg(corner.name())));
        // the ring on the box's left edge, darker than the fill
        const QColor ring = image.pixelColor(qFloor(words.left() - pad.width()), qRound(words.center().y()));
        QVERIFY2(luma(ring) < luma(ectoGreen) - 25, qPrintable(u"ring %1"_s.arg(ring.name())));
        // past the box, the picture
        QVERIFY(near(image.pixelColor(qRound(words.left() - pad.width() - 4), qRound(words.center().y())), base));
    }
}

void AnnotationLookTest::textWithoutLabel()
{
    auto doc = document(QColor(24, 24, 27));
    doc->tool()->setType(AnnotationTool::TextTool);
    doc->tool()->setShadow(false);
    doc->beginItem(QPointF(40, 60));
    doc->finishItem();
    auto item = doc->selectedItemWrapper();
    item->setText(u"Plain words"_s);
    item->setFillColor(Qt::transparent);
    item->commitChanges();
    const QRectF words = item->geometryPath().boundingRect();
    const QRectF box = item->mousePath().boundingRect();
    QVERIFY2(box.width() < words.width() + 4, "without its fill the mark is just its words");
    doc->deselectItem();
    const QImage image = doc->renderToImage();
    QVERIFY(near(image.pixelColor(qRound(words.left() - 3), qRound(words.center().y())), QColor(24, 24, 27)));
    // the brightest pixel of the words is the words' own color, Ecto green
    QColor brightest(0, 0, 0);
    for (int y = words.top(); y < words.bottom(); ++y) {
        for (int x = words.left(); x < words.right(); ++x) {
            if (luma(image.pixelColor(x, y)) > luma(brightest)) {
                brightest = image.pixelColor(x, y);
            }
        }
    }
    QVERIFY2(near(brightest, ectoGreen, 12), qPrintable(u"plain words %1"_s.arg(brightest.name())));
}

void AnnotationLookTest::labelStaysPut()
{
    // the words keep their place while their font, their words and their label change
    auto doc = document(Qt::white);
    doc->tool()->setType(AnnotationTool::TextTool);
    doc->beginItem(QPointF(40, 60));
    doc->finishItem();
    auto item = doc->selectedItemWrapper();
    item->setText(u"Run this first"_s);
    item->commitChanges();
    const QPointF start = item->geometryPath().boundingRect().topLeft();
    auto same = [&](const char *what) {
        const QPointF now = item->geometryPath().boundingRect().topLeft();
        QVERIFY2((now - start).manhattanLength() < 0.01, qPrintable(u"%1: the words moved from %2,%3 to %4,%5"_s.arg(QString::fromLatin1(what)).arg(start.x()).arg(start.y()).arg(now.x()).arg(now.y())));
    };
    QFont bigger = item->font();
    bigger.setPointSize(20);
    item->setFont(bigger);
    item->commitChanges();
    same("a bigger font");
    item->setFillColor(Qt::transparent);
    item->commitChanges();
    same("the label off");
    item->setFillColor(ectoGreen);
    item->commitChanges();
    same("the label on again");
    item->setText(u"Run this first, then the rest"_s);
    item->commitChanges();
    same("longer words");
    for (int i = 0; i < 6; ++i) {
        item->setFillColor(i % 2 ? ectoGreen : QColor(Qt::transparent));
        item->commitChanges();
    }
    same("the label toggled six times, ending on");
    // and the label's box grows with it
    const QRectF words = item->geometryPath().boundingRect();
    const QRectF box = item->mousePath().boundingRect();
    QVERIFY2(box.left() < words.left() - 2 && box.right() > words.right() + 2, "the label's box surrounds the words");
}

void AnnotationLookTest::emptyTextDrawsNothing()
{
    // a click with Text and no words typed leaves no box in the picture
    auto doc = document(Qt::white);
    doc->tool()->setType(AnnotationTool::TextTool);
    doc->beginItem(QPointF(40, 60));
    doc->finishItem();
    doc->deselectItem();
    const QImage image = doc->renderToImage();
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (!near(image.pixelColor(x, y), Qt::white, 2)) {
                QFAIL(qPrintable(u"pixel %1,%2 is %3"_s.arg(x).arg(y).arg(image.pixelColor(x, y).name())));
            }
        }
    }
}

void AnnotationLookTest::wordsReadOnAnyFill()
{
    // near-black on light fills, white on dark ones
    for (const auto &[fill, dark] : std::initializer_list<std::pair<QColor, bool>>{
             {ectoGreen, true}, {QColor(u"#facc15"_s), true}, {QColor(u"#ffffff"_s), true}, {QColor(u"#a1a1aa"_s), true}, {QColor(u"#22d3ee"_s), true},
             {QColor(u"#ef4444"_s), false}, {QColor(u"#3b82f6"_s), false}, {QColor(u"#a855f7"_s), false}, {QColor(u"#0a0a0a"_s), false}}) {
        const QColor words = Traits::labelTextColor(fill);
        QVERIFY2((luma(words) < 40) == dark, qPrintable(u"words %1 on %2"_s.arg(words.name(), fill.name())));
    }
}

namespace
{
// the rows of a rect holding a dark pixel (the words on a green label, or on white)
QList<int> inkRows(const QImage &image, const QRect &rect)
{
    QList<int> rows;
    for (int y = std::max(0, rect.top()); y <= std::min(image.height() - 1, rect.bottom()); ++y) {
        for (int x = std::max(0, rect.left()); x <= std::min(image.width() - 1, rect.right()); ++x) {
            if (luma(image.pixelColor(x, y)) < 110) {
                rows << y;
                break;
            }
        }
    }
    return rows;
}

// the rightmost column of a rect holding a dark pixel, or -1
int inkRight(const QImage &image, const QRect &rect)
{
    for (int x = std::min(image.width() - 1, rect.right()); x >= std::max(0, rect.left()); --x) {
        for (int y = std::max(0, rect.top()); y <= std::min(image.height() - 1, rect.bottom()); ++y) {
            if (luma(image.pixelColor(x, y)) < 110) {
                return x;
            }
        }
    }
    return -1;
}

Traits::Text textOf(SelectedItemWrapper *item)
{
    Traits::Text text;
    text.emplace<Traits::Text::String>(item->text());
    text.font = item->font();
    text.brush = Qt::black;
    text.box = item->isTextBox() ? item->textBoxSize() : QSizeF();
    text.alignment = Qt::Alignment(item->textAlignment());
    return text;
}

const QString sentence = u"Click Record to start, then pick the window you want to keep on screen."_s;
}

SelectedItemWrapper *AnnotationLookTest::drawBox(AnnotationDocument *doc, QPointF from, QPointF to, const QString &words)
{
    doc->tool()->setType(AnnotationTool::TextTool);
    doc->tool()->setShadow(false);
    doc->beginItem(from);
    // the pointer passes through on its way, as a real drag does
    doc->continueItem((from + to) / 2);
    doc->continueItem(to);
    doc->finishItem();
    auto item = doc->selectedItemWrapper();
    item->setText(words);
    item->commitChanges();
    return item;
}

void AnnotationLookTest::shortDragIsAClick()
{
    // Through the marking canvas itself, as the overlay and the window use it, zoomed out (the window
    // fits a big screenshot at about 40%), at 100% and zoomed in: a press, a wobble of 5 screen pixels
    // and a release is a click, today's label where it pressed; a 40-pixel drag is a box, as big on
    // the picture as the drag was on screen.
    for (const qreal zoom : {0.4, 1.0, 2.0}) {
        for (const int distance : {5, 40}) {
            auto doc = document(Qt::white, {800, 600});
            doc->tool()->setType(AnnotationTool::TextTool);
            QQuickWindow window;
            AnnotationViewport viewport(window.contentItem());
            viewport.setDocument(doc.get());
            viewport.setSize(QSizeF(800, 600));
            // the canvas's own units are the picture's; on screen they are scaled by the zoom
            auto send = [&](QEvent::Type type, QPointF screen, Qt::MouseButtons buttons) {
                QMouseEvent event(type, screen / zoom, screen, screen, Qt::LeftButton, buttons, Qt::NoModifier);
                QCoreApplication::sendEvent(&viewport, &event);
            };
            const QPointF press(80, 80);
            send(QEvent::MouseButtonPress, press, Qt::LeftButton);
            const QRectF before = doc->selectedItemWrapper()->geometryPath().boundingRect();
            for (int step = 1; step <= 4; ++step) {
                send(QEvent::MouseMove, press + QPointF(distance * step / 4.0, distance * step / 16.0), Qt::LeftButton);
            }
            send(QEvent::MouseButtonRelease, press + QPointF(distance, distance / 4.0), Qt::NoButton);
            auto item = doc->selectedItemWrapper();
            const QString what = u"%1 px at %2%"_s.arg(distance).arg(zoom * 100);
            if (distance < Traits::textBoxDragThreshold) {
                QVERIFY2(!item->isTextBox(), qPrintable(what + u": a wobble is a click"_s));
                QCOMPARE(item->geometryPath().boundingRect().topLeft(), before.topLeft());
            } else {
                QVERIFY2(item->isTextBox(), qPrintable(what + u": a drag is a box"_s));
                const qreal expected = std::max(distance / zoom, Traits::textBoxMinimumSize(item->font()).width());
                QVERIFY2(std::abs(item->textOutsideRect().width() - expected) < 1, qPrintable(what + u": %1 wide on the picture, %2 expected"_s.arg(item->textOutsideRect().width()).arg(expected)));
            }
        }
    }
}

void AnnotationLookTest::dragDrawsABox()
{
    for (const bool backwards : {false, true}) {
        auto doc = document(Qt::white, {420, 260});
        // the same box, dragged from its top left or from its bottom right
        auto item = backwards ? drawBox(doc.get(), QPointF(240, 110), QPointF(40, 40), sentence) : drawBox(doc.get(), QPointF(40, 40), QPointF(240, 110), sentence);
        QVERIFY(item->isTextBox());
        const QRectF outside = item->textOutsideRect();
        QCOMPARE(outside.topLeft(), QPointF(40, 40));
        QCOMPARE(outside.width(), 200.0);
        QVERIFY2(outside.height() >= 70, qPrintable(u"the box keeps its drawn height (%1)"_s.arg(outside.height())));
        const QRectF words = item->geometryPath().boundingRect();
        const QSizeF pad = Traits::textLabelPadding(item->font());
        const auto lines = Traits::textBoxLines(textOf(item), words.width());
        QVERIFY2(lines.size() >= 3, qPrintable(u"the words wrap at the box's width (%1 lines)"_s.arg(lines.size())));
        doc->deselectItem();
        const QImage image = doc->renderToImage();
        save(image, backwards ? u"box-backwards"_s : u"box"_s);
        // the label is the drawn box: green at its far corners' insides, the picture just past them
        QVERIFY(near(image.pixelColor(qRound(outside.right() - pad.width() / 2), qRound(outside.top() + pad.height() + 2)), ectoGreen));
        QVERIFY(near(image.pixelColor(qRound(outside.left() + pad.width() / 2), qRound(outside.bottom() - pad.height())), ectoGreen));
        QVERIFY(near(image.pixelColor(qRound(outside.right() + 3), qRound(outside.center().y())), Qt::white));
        QVERIFY(near(image.pixelColor(qRound(outside.center().x()), qRound(outside.bottom() + 3)), Qt::white));
        // every line the layout has is painted, each in its own band, and nothing past the words' width
        const qreal lineHeight = lines.size() > 1 ? lines[1].second - lines[0].second : words.height();
        for (int i = 0; i < lines.size(); ++i) {
            const QRect band = QRectF(words.left(), words.top() + lines[i].second, words.width(), lineHeight).toAlignedRect();
            QVERIFY2(!inkRows(image, band).isEmpty(), qPrintable(u"line %1 is painted"_s.arg(i + 1)));
        }
        QVERIFY2(inkRows(image, QRectF(words.left(), words.top() + lines.size() * lineHeight + 1, words.width(), outside.bottom() - words.top() - lines.size() * lineHeight).toAlignedRect()).isEmpty(),
                 "no words below the last line");
        const int right = inkRight(image, outside.toAlignedRect());
        QVERIFY2(right <= words.right() + 1, qPrintable(u"words end at %1, the box's words at %2"_s.arg(right).arg(words.right())));
    }
}

void AnnotationLookTest::boxGrowsSoWordsShow()
{
    // a box drawn too short for its words grows down; none of them is hidden or outside
    auto doc = document(Qt::white, {420, 300});
    auto item = drawBox(doc.get(), QPointF(40, 40), QPointF(200, 62), sentence + u' ' + sentence);
    const QRectF outside = item->textOutsideRect();
    QCOMPARE(outside.width(), 160.0);
    QVERIFY2(outside.height() > 60, qPrintable(u"grew to %1"_s.arg(outside.height())));
    doc->deselectItem();
    const QImage image = doc->renderToImage();
    save(image, u"box-grown"_s);
    const QList<int> rows = inkRows(image, QRect(0, 0, image.width(), image.height()));
    QVERIFY(!rows.isEmpty());
    QVERIFY2(rows.last() < outside.bottom() && rows.first() > outside.top(), "every word inside the box");
    QVERIFY(near(image.pixelColor(qRound(outside.center().x()), qRound(outside.bottom() - 2)), ectoGreen));
}

void AnnotationLookTest::boxBreaksLongWords()
{
    // a word longer than the box (a link, a path, a checksum with nowhere to break) breaks inside
    // it instead of running out
    for (const QString &word : {u"https://invent.kde.org/graphics/spectacle/-/merge_requests/123456"_s,
                                u"9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08"_s}) {
        auto doc = document(Qt::white, {420, 260});
        auto item = drawBox(doc.get(), QPointF(40, 40), QPointF(190, 80), word);
        const QRectF outside = item->textOutsideRect();
        const QRectF words = item->geometryPath().boundingRect();
        QCOMPARE(outside.width(), 150.0);
        QVERIFY(Traits::textBoxLines(textOf(item), words.width()).size() >= 2);
        doc->deselectItem();
        const QImage image = doc->renderToImage();
        save(image, u"box-long-word-"_s + word.left(5).remove(u':'));
        const int right = inkRight(image, QRect(0, 0, image.width(), image.height()));
        QVERIFY2(right <= words.right() + 1, qPrintable(u"%1: ink to x=%2, the words end at %3"_s.arg(word.left(12)).arg(right).arg(words.right())));
    }
}

void AnnotationLookTest::resizeRewraps()
{
    auto doc = document(Qt::white, {420, 300});
    auto item = drawBox(doc.get(), QPointF(40, 40), QPointF(300, 80), sentence);
    const QRectF wide = item->textOutsideRect();
    const int wideLines = Traits::textBoxLines(textOf(item), item->geometryPath().boundingRect().width()).size();
    doc->deselectItem();
    const QImage before = doc->renderToImage();
    // a handle drag: narrower, the same top left
    doc->selectItem(QRectF(wide.center(), QSizeF(1, 1)));
    item = doc->selectedItemWrapper();
    QVERIFY(item->isTextBox());
    item->setTextBox(QRectF(wide.topLeft(), QSizeF(120, wide.height())));
    item->commitChanges();
    QCOMPARE(item->textOutsideRect().width(), 120.0);
    QCOMPARE(item->textOutsideRect().topLeft(), wide.topLeft());
    const int narrowLines = Traits::textBoxLines(textOf(item), item->geometryPath().boundingRect().width()).size();
    QVERIFY2(narrowLines > wideLines, qPrintable(u"%1 lines at 120 wide, %2 at %3"_s.arg(narrowLines).arg(wideLines).arg(wide.width())));
    QVERIFY(item->textOutsideRect().height() > wide.height());
    // smaller than the smallest box: held at the smallest
    item->setTextBox(QRectF(wide.topLeft(), QSizeF(4, 4)));
    QCOMPARE(item->textOutsideRect().width(), Traits::textBoxMinimumSize(item->font()).width());
    item->commitChanges();
    doc->deselectItem();
    // undo puts the box back as it was
    doc->undo();
    doc->undo();
    QCOMPARE(doc->renderToImage(), before);
}

void AnnotationLookTest::clickLabelBecomesABox()
{
    // a handle on a click's label makes it a box; its words stay where they are
    auto doc = document(Qt::white, {420, 260});
    doc->tool()->setType(AnnotationTool::TextTool);
    doc->beginItem(QPointF(40, 60));
    doc->finishItem();
    auto item = doc->selectedItemWrapper();
    item->setText(sentence);
    item->commitChanges();
    QVERIFY(!item->isTextBox());
    const QRectF words = item->geometryPath().boundingRect();
    const QRectF outside = item->textOutsideRect();
    item->setTextBox(QRectF(outside.topLeft(), QSizeF(180, outside.height())));
    QVERIFY(item->isTextBox());
    QCOMPARE(item->geometryPath().boundingRect().topLeft(), words.topLeft());
    QVERIFY(item->geometryPath().boundingRect().height() > words.height());
}

void AnnotationLookTest::editorBreaksLikePainting()
{
    // The editing box is Qt Quick's TextEdit set as TextTool.qml sets it; the painted lines must
    // start where its lines start, at the same heights, or the cursor wanders off the words.
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"QML(
import QtQuick
TextEdit {
    id: edit
    textFormat: TextEdit.PlainText
    horizontalAlignment: TextEdit.AlignLeft
    verticalAlignment: TextEdit.AlignTop
    wrapMode: TextEdit.Wrap
    renderType: TextEdit.NativeRendering
}
)QML"_ba,
                      QUrl());
    std::unique_ptr<QObject> edit(component.create());
    QVERIFY2(edit, qPrintable(component.errorString()));
    auto *item = qobject_cast<QQuickItem *>(edit.get());
    const QStringList texts{sentence,
                            u"https://invent.kde.org/graphics/spectacle/-/merge_requests/123456 and more"_s,
                            u"Two paragraphs here.\nThe second one is a little longer than the first, so it wraps."_s,
                            u"Tabs\tbetween\twords\tand   several   spaces   between   these   words"_s,
                            u"A trailing space at the end of a line should not move the break "_s};
    QFont big(u"Geist"_s, 18, QFont::Normal);
    auto doc = document(Qt::white);
    doc->tool()->setType(AnnotationTool::TextTool);
    for (const QFont &font : {doc->tool()->font(), big}) {
        for (const QString &words : texts) {
            for (qreal width = 60; width <= 320; width += 37) {
                Traits::Text text;
                text.emplace<Traits::Text::String>(words);
                text.font = font;
                text.brush = Qt::black;
                // a box (the editor wraps): its words wrap at the width
                text.box = QSizeF(width, 40);
                edit->setProperty("font", font);
                // as TextTool.qml sets it (Utils.textTabStopDistance)
                edit->setProperty("tabStopDistance", qreal(qRound(QFontMetricsF(font).horizontalAdvance(u'x') * 8)));
                edit->setProperty("text", words);
                item->setWidth(width);
                QList<std::pair<int, qreal>> editorLines;
                qreal lastY = -1;
                for (int i = 0; i <= words.size(); ++i) {
                    QRectF r;
                    QMetaObject::invokeMethod(edit.get(), "positionToRectangle", Q_RETURN_ARG(QRectF, r), Q_ARG(int, i));
                    if (r.y() > lastY + 0.5) {
                        editorLines.append({i, r.y()});
                        lastY = r.y();
                    }
                }
                const auto painted = Traits::textBoxLines(text, width);
                QList<int> editorStarts, paintedStarts;
                QList<qreal> editorTops, paintedTops;
                for (const auto &[start, top] : editorLines) {
                    editorStarts << start;
                    editorTops << top;
                }
                for (const auto &[start, top] : painted) {
                    paintedStarts << start;
                    paintedTops << top;
                }
                const QString what = u"%1 at %2 px, %3 pt"_s.arg(words.left(24)).arg(width).arg(font.pointSizeF());
                QVERIFY2(editorStarts == paintedStarts, qPrintable(what + u": lines start at %1 painted, %2 in the editor"_s.arg(QDebug::toString(paintedStarts), QDebug::toString(editorStarts))));
                QVERIFY2(editorTops == paintedTops, qPrintable(what + u": line tops %1 painted, %2 in the editor"_s.arg(QDebug::toString(paintedTops), QDebug::toString(editorTops))));
                QCOMPARE(edit->property("contentHeight").toReal(), Traits::textBoxWordsSize(text, width).height());
            }
        }
    }
}

namespace
{
// The library's real TextTool.qml, on a marking canvas in a window that is never shown
struct LiveTextTool {
    QQuickWindow window;
    QQmlEngine engine;
    std::unique_ptr<QObject> viewport;
    QQuickItem *textTool = nullptr;
    QString error;
    explicit LiveTextTool(AnnotationDocument *doc)
    {
        // i18nc and the rest, as the app's QML engine has them
        engine.rootContext()->setContextObject(new KLocalizedQmlContext(&engine));
        QQmlComponent component(&engine);
        component.setData(R"QML(
import QtQuick
import org.kde.kquickimageeditor.ectolens
AnnotationViewport {
    id: viewport
    width: 800
    height: 600
    viewportRect: Qt.rect(0, 0, width, height)
    Item {
        TextTool {
            objectName: "textTool"
            viewport: viewport
        }
    }
}
)QML"_ba,
                          QUrl());
        viewport.reset(component.create());
        if (!viewport) {
            error = component.errorString();
            return;
        }
        auto *item = qobject_cast<QQuickItem *>(viewport.get());
        item->setParentItem(window.contentItem());
        item->setProperty("document", QVariant::fromValue(doc));
        textTool = item->findChild<QQuickItem *>(u"textTool"_s);
    }
    // the editing box: the TextTool loader's item
    QQuickItem *editor() const
    {
        return textTool ? textTool->property("item").value<QQuickItem *>() : nullptr;
    }
    // every showing item whose name starts so, by the visual tree
    QList<QQuickItem *> findAll(const QString &prefix) const
    {
        QList<QQuickItem *> found;
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
            if (!item) {
                return;
            }
            if (item->objectName().startsWith(prefix) && item->isVisible()) {
                found << item;
            }
            for (auto child : item->childItems()) {
                walk(child);
            }
        };
        walk(qobject_cast<QQuickItem *>(viewport.get()));
        return found;
    }
    // by the visual tree, as a Repeater's items are not QObject children of the canvas
    QQuickItem *find(const QString &name) const
    {
        std::function<QQuickItem *(QQuickItem *)> walk = [&](QQuickItem *item) -> QQuickItem * {
            if (!item) {
                return nullptr;
            }
            if (item->objectName() == name && item->isVisible()) {
                return item;
            }
            for (auto child : item->childItems()) {
                if (auto found = walk(child)) {
                    return found;
                }
            }
            return nullptr;
        };
        return walk(qobject_cast<QQuickItem *>(viewport.get()));
    }
};

// where the editor's lines start (a character index) and their tops, from its own layout
QList<std::pair<int, qreal>> editorLines(QQuickItem *editor)
{
    QList<std::pair<int, qreal>> lines;
    const QString text = editor->property("text").toString();
    qreal lastY = -1;
    for (int i = 0; i <= text.size(); ++i) {
        QRectF r;
        QMetaObject::invokeMethod(editor, "positionToRectangle", Q_RETURN_ARG(QRectF, r), Q_ARG(int, i));
        if (r.y() > lastY + 0.5) {
            lines.append({i, r.y()});
            lastY = r.y();
        }
    }
    return lines;
}
}

void AnnotationLookTest::bigFontKeepsRoomForWords()
{
    // a much bigger font in a small box never stacks the words a letter or two to a line: the words
    // keep at least two line heights of width and the box widens around them
    auto doc = document(Qt::white, {800, 600});
    auto item = drawBox(doc.get(), QPointF(40, 40), QPointF(160, 80), u"hello world"_s);
    for (const int size : {11, 48, 144}) {
        QFont font = item->font();
        font.setPointSize(size);
        item->setFont(font);
        const QRectF words = item->geometryPath().boundingRect();
        const qreal lineHeight = QFontMetricsF(font).height();
        QVERIFY2(words.width() >= std::ceil(lineHeight * 2) - 1,
                 qPrintable(u"%1 pt: words %2 wide, two lines are %3"_s.arg(size).arg(words.width()).arg(lineHeight * 2)));
        // no line holds fewer than two letters, but the last
        const auto lines = Traits::textBoxLines(textOf(item), words.width());
        for (int i = 0; i + 1 < lines.size(); ++i) {
            QVERIFY2(lines[i + 1].first - lines[i].first >= 2, qPrintable(u"%1 pt: line %2 holds one letter"_s.arg(size).arg(i + 1)));
        }
        QCOMPARE(item->textOutsideRect().left(), 40.0);
    }
}

void AnnotationLookTest::boxWithoutLabel()
{
    // with its label (fill) off, a box is still a box: its words wrap at the same place, nothing but
    // the words is painted, and all of the box takes clicks
    auto doc = document(QColor(24, 24, 27), {420, 260});
    auto item = drawBox(doc.get(), QPointF(40, 40), QPointF(200, 110), sentence);
    const QRectF labelled = item->geometryPath().boundingRect();
    item->setFillColor(Qt::transparent);
    item->commitChanges();
    QVERIFY(item->isTextBox());
    QCOMPARE(item->geometryPath().boundingRect(), labelled);
    QCOMPARE(item->textOutsideRect().topLeft(), QPointF(40, 40));
    QVERIFY(item->mousePath().boundingRect().contains(QRectF(41, 41, 158, 68)));
    doc->deselectItem();
    const QImage image = doc->renderToImage();
    save(image, u"box-no-label"_s);
    // the words are Ecto green on the dark picture, and the picture shows between and around them
    QVERIFY(near(image.pixelColor(43, 43), QColor(24, 24, 27)));
    QVERIFY(near(image.pixelColor(197, 105), QColor(24, 24, 27)));
    int green = 0;
    for (int y = 40; y < 110; ++y) {
        for (int x = 40; x < 200; ++x) {
            green += near(image.pixelColor(x, y), ectoGreen, 30) ? 1 : 0;
        }
    }
    QVERIFY2(green > 100 && green < 160 * 70 / 3, qPrintable(u"%1 green pixels: words, no box"_s.arg(green)));
}

void AnnotationLookTest::textToolBreaksLikePainting()
{
    // The real editing box (TextTool.qml, as the overlay and the window load it) over a drawn box:
    // its lines must start where the painted lines start, at the same heights, with the box's
    // words width, before and after a font change (its tab width once kept the first font's).
    auto doc = document(Qt::white, {800, 600});
    auto item = drawBox(doc.get(), QPointF(40, 40), QPointF(260, 120), u"Tabs\tbetween\twords and a sentence long enough to wrap twice, https://invent.kde.org/graphics/spectacle"_s);
    LiveTextTool live(doc.get());
    QVERIFY2(live.textTool, qPrintable(live.error));
    QTRY_VERIFY(live.editor());
    QQuickItem *editor = live.editor();
    for (const QFont &font : {item->font(), QFont(u"Geist"_s, 17, QFont::Normal)}) {
        item->setFont(font);
        QTRY_COMPARE(editor->property("font").value<QFont>().pointSizeF(), font.pointSizeF());
        const QRectF words = item->geometryPath().boundingRect();
        QCOMPARE(editor->width(), words.width());
        QCOMPARE(editor->property("text").toString(), item->text());
        const auto painted = Traits::textBoxLines(textOf(item), words.width());
        const auto edited = editorLines(editor);
        QVERIFY2(painted.size() >= 3, "the words wrap");
        QVERIFY2(edited == painted, qPrintable(u"%1 pt: painted %2, editor %3"_s.arg(font.pointSizeF()).arg(QDebug::toString(painted), QDebug::toString(edited))));
    }
}

void AnnotationLookTest::boxHandleTakesAReleaseOnlyDrag()
{
    // A drag on a box's handle whose motion reaches Qt only with the release (a quick flick, or merged
    // motion) still resizes the box: the off-screen textbox run caught a DragHandler that ignored it.
    auto doc = document(Qt::white, {800, 600});
    // drawn short, so the words have already grown it when the handle is taken
    auto item = drawBox(doc.get(), QPointF(40, 40), QPointF(300, 62), sentence + u' ' + sentence);
    const QSizeF drawn = item->textBoxSize();
    QVERIFY2(item->textOutsideRect().height() > drawn.height(), "the words grew the box");
    LiveTextTool live(doc.get());
    QVERIFY2(live.textTool, qPrintable(live.error));
    QTRY_VERIFY(live.find(u"textBoxHandle4"_s));
    QQuickItem *handle = live.find(u"textBoxHandle4"_s);
    QQuickItem *area = handle->childItems().value(0);
    QVERIFY2(area && area->inherits("QQuickMouseArea"), "the right handle takes the pointer");
    const QRectF before = item->textOutsideRect();
    const QPointF center(area->width() / 2, area->height() / 2);
    const QPointF moved = center - QPointF(100, 0);
    QMouseEvent press(QEvent::MouseButtonPress, center, area->mapToScene(center), area->mapToScene(center), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(area, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, moved, area->mapToScene(moved), area->mapToScene(moved), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(area, &release);
    QCOMPARE(item->textOutsideRect().left(), before.left());
    QVERIFY2(std::abs(item->textOutsideRect().width() - (before.width() - 100)) < 1,
             qPrintable(u"the box is %1 wide after the drag, %2 expected"_s.arg(item->textOutsideRect().width()).arg(before.width() - 100)));
    // a side handle keeps the height the box was drawn to (not the height its words grew it to), so
    // it can shrink back as words are taken out
    QCOMPARE(item->textBoxSize().height(), drawn.height());
    item->setText(u"Short"_s);
    QCOMPARE(item->textOutsideRect().height(), drawn.height());
}

void AnnotationLookTest::handlesStayOffTheWords()
{
    // At the window's zooms (40% fits a big screenshot), at 100% and at 200%: no handle takes the
    // pointer over a box's words (only outside the box and a thin band along its edge), and the move
    // handle stands clear of the box's left handles.
    for (const qreal zoom : {0.4, 1.0, 2.0}) {
        auto doc = document(Qt::white, {800, 600});
        drawBox(doc.get(), QPointF(40, 40), QPointF(300, 120), sentence);
        LiveTextTool live(doc.get());
        QVERIFY2(live.textTool, qPrintable(live.error));
        qobject_cast<QQuickItem *>(live.viewport.get())->setScale(zoom);
        QTRY_VERIFY(live.find(u"textBoxHandle4"_s));
        const QString what = u"at %1%"_s.arg(zoom * 100);
        const QRectF words = live.editor()->mapRectToScene(QRectF(0, 0, live.editor()->width(), live.editor()->height()));
        // every handle the box shows, found by name; each name ends in its Qt::Edges
        // as many showing as the box makes (its Repeater's count), none hidden or lost
        const auto handles = live.findAll(u"textBoxHandle"_s);
        const auto maker = live.find(u"boxHandleMaker"_s);
        QVERIFY2(maker, "the box's handles are made by a Repeater named boxHandleMaker");
        QVERIFY2(handles.size() == maker->property("count").toInt() && handles.size() >= 4,
                 qPrintable(what + u": %1 handles showing of %2"_s.arg(handles.size()).arg(maker->property("count").toInt())));
        for (QQuickItem *handle : handles) {
            const int edges = handle->objectName().mid(13).toInt();
            QQuickItem *area = handle->childItems().value(0);
            QVERIFY2(area && area->inherits("QQuickMouseArea"), qPrintable(what + u": handle %1 takes the pointer"_s.arg(edges)));
            // every point of the handle's square that lies over the words: none takes the pointer
            for (qreal x = 0; x <= area->width(); x += area->width() / 8) {
                for (qreal y = 0; y <= area->height(); y += area->height() / 8) {
                    const QPointF scene = area->mapToScene(QPointF(x, y));
                    if (words.contains(scene)) {
                        QVERIFY2(!area->contains(QPointF(x, y)), qPrintable(what + u": handle %1 takes the pointer over the words at %2,%3"_s.arg(edges).arg(scene.x()).arg(scene.y())));
                    }
                }
            }
            // its own center, on the box's edge, does
            QVERIFY2(area->contains(QPointF(area->width() / 2, area->height() / 2)), qPrintable(what + u": handle %1 misses its center"_s.arg(edges)));
        }
        // the move handle stands beside the box (on the right of right-aligned words, on the left
        // otherwise), clear of the words and of every handle, at each alignment
        auto item = doc->selectedItemWrapper();
        for (const int alignment : {int(Qt::AlignLeft), int(Qt::AlignHCenter), int(Qt::AlignRight)}) {
            item->setTextAlignment(alignment | Qt::AlignTop);
            QTRY_COMPARE(live.editor()->property("horizontalAlignment").toInt(), alignment);
            QQuickItem *move = live.find(u"textMoveHandle"_s);
            QVERIFY(move);
            const QRectF moveRect = move->mapRectToScene(QRectF(0, 0, move->width(), move->height()));
            const QString at = what + u", aligned %1"_s.arg(alignment);
            QVERIFY2(!moveRect.intersects(words), qPrintable(at + u": the move handle %1 covers the words %2"_s.arg(QDebug::toString(moveRect), QDebug::toString(words))));
            for (QQuickItem *handle : handles) {
                const int edges = handle->objectName().mid(13).toInt();
                const QRectF handleRect = handle->mapRectToScene(QRectF(0, 0, handle->width(), handle->height()));
                QVERIFY2(!moveRect.intersects(handleRect), qPrintable(at + u": the move handle %1 covers the handle %2 %3"_s.arg(QDebug::toString(moveRect)).arg(edges).arg(QDebug::toString(handleRect))));
            }
        }
    }
}

namespace
{
// the editor's cursor before each character (and after the last): its x and its line's top
QList<QPointF> editorCursorPoints(QQuickItem *editor)
{
    QList<QPointF> points;
    const QString text = editor->property("text").toString();
    for (int i = 0; i <= text.size(); ++i) {
        QRectF r;
        QMetaObject::invokeMethod(editor, "positionToRectangle", Q_RETURN_ARG(QRectF, r), Q_ARG(int, i));
        points << r.topLeft();
    }
    return points;
}

// a click's label (no box) with these words
SelectedItemWrapper *clickLabel(AnnotationDocument *doc, QPointF at, const QString &words)
{
    doc->tool()->setType(AnnotationTool::TextTool);
    doc->tool()->setShadow(false);
    doc->beginItem(at);
    doc->finishItem();
    auto item = doc->selectedItemWrapper();
    item->setText(words);
    item->commitChanges();
    return item;
}

// the leftmost and rightmost columns holding ink in a rect (dark on a light fill), or -1
std::pair<int, int> inkColumns(const QImage &image, const QRect &rect)
{
    int left = -1, right = -1;
    for (int x = std::max(0, rect.left()); x <= std::min(image.width() - 1, rect.right()); ++x) {
        for (int y = std::max(0, rect.top()); y <= std::min(image.height() - 1, rect.bottom()); ++y) {
            if (luma(image.pixelColor(x, y)) < 110) {
                if (left < 0) {
                    left = x;
                }
                right = x;
                break;
            }
        }
    }
    return {left, right};
}

const QString hebrew = u"שלום עולם! זו שורה שנייה, ארוכה מעט יותר מהראשונה."_s;
const QString arabic = u"مرحبا بالعالم 2026 (نص تجريبي)، سطر أطول قليلا."_s;
}

void AnnotationLookTest::editorPlacesWordsLikePainting()
{
    // The real editing box (TextTool.qml) over a drawn box and over a click's label, at each alignment
    // (up and down too, in the box, which is drawn taller than its words), with left-to-right,
    // right-to-left (Hebrew, Arabic with digits and brackets) and mixed words, a tab and a second line:
    // the cursor before every character stands where the painted words have that character, so typing,
    // clicking and selecting land on the words people see.
    const QStringList texts{u"Tabs\tbetween words, and a sentence long enough to wrap.\nA second, shorter line."_s, hebrew, arabic,
                            u"Mixed: שלום and مرحبا in one line, then more words to wrap."_s};
    for (const bool box : {true, false}) {
        auto doc = document(Qt::white, {800, 600});
        auto item = box ? drawBox(doc.get(), QPointF(40, 40), QPointF(300, 260), texts.first()) : clickLabel(doc.get(), QPointF(300, 400), texts.first());
        LiveTextTool live(doc.get());
        QVERIFY2(live.textTool, qPrintable(live.error));
        QTRY_VERIFY(live.editor());
        QQuickItem *editor = live.editor();
        const QList<int> verticals = box ? QList<int>{Qt::AlignTop, Qt::AlignVCenter, Qt::AlignBottom} : QList<int>{Qt::AlignTop};
        for (const int vertical : verticals) {
            for (const int horizontal : {int(Qt::AlignLeft), int(Qt::AlignHCenter), int(Qt::AlignRight)}) {
                item->setTextAlignment(horizontal | vertical);
                QTRY_COMPARE(editor->property("horizontalAlignment").toInt(), horizontal);
                QTRY_COMPARE(editor->property("verticalAlignment").toInt(), vertical);
                for (const QString &words : texts) {
                    item->setText(words);
                    QTRY_COMPARE(editor->property("text").toString(), words);
                    const QRectF rect = item->geometryPath().boundingRect();
                    QTRY_COMPARE(editor->width(), rect.width());
                    QTRY_COMPARE(editor->height(), rect.height());
                    const auto painted = Traits::textCursorPoints(textOf(item), rect.width(), rect.height());
                    const auto edited = editorCursorPoints(editor);
                    QCOMPARE(edited.size(), painted.size());
                    const QString what = u"%1, aligned %2, %3"_s.arg(box ? u"box"_s : u"label"_s).arg(horizontal | vertical).arg(words.left(16));
                    for (int i = 0; i < painted.size(); ++i) {
                        QVERIFY2(std::abs(edited[i].x() - painted[i].x()) < 0.5 && std::abs(edited[i].y() - painted[i].y()) < 0.5,
                                 qPrintable(what + u": the cursor before character %1 is at %2,%3 in the editor, the words have it at %4,%5"_s.arg(i)
                                                       .arg(edited[i].x()).arg(edited[i].y()).arg(painted[i].x()).arg(painted[i].y())));
                    }
                }
            }
        }
    }
}

void AnnotationLookTest::labelFitsAnyFontSize()
{
    // The editing box lays words out with the font size rounded to Qt Quick's 0.5 pt; a label measured
    // with the size as given (a font carried over from Spectacle can be 10.3 pt) came out narrower or wider
    // than the words. At sizes on and off the steps, the label is exactly as wide as the editing box's words.
    auto doc = document(Qt::white, {800, 400});
    auto item = clickLabel(doc.get(), QPointF(100, 100), u"Measured words, then more"_s);
    LiveTextTool live(doc.get());
    QVERIFY2(live.textTool, qPrintable(live.error));
    QTRY_VERIFY(live.editor());
    // (10.8 and 6.3 pt land on another whole pixel size than their rounded 11 and 6.5 pt)
    for (const qreal size : {10.3, 10.5, 10.8, 6.3, 11.2, 13.74, 20.0}) {
        QFont font = item->font();
        font.setPointSizeF(size);
        item->setFont(font);
        QTRY_COMPARE(live.editor()->property("font").value<QFont>().pointSizeF(), size);
        const qreal words = item->geometryPath().boundingRect().width();
        // the editing box's own measure of its words, at its rounded size
        QTRY_VERIFY(live.editor()->property("contentWidth").toReal() > 0);
        const qreal edited = live.editor()->property("contentWidth").toReal();
        QVERIFY2(std::abs(words - edited) < 0.5, qPrintable(u"%1 pt: the label's words are %2 wide, the editing box's %3"_s.arg(size).arg(words).arg(edited)));
    }
}

void AnnotationLookTest::alignmentMovesTheWords()
{
    // On the picture: a box's short words sit at its left, center or right; in a two-line label the
    // shorter line sits at the label's left, center or right under the longer one.
    for (const int alignment : {int(Qt::AlignLeft), int(Qt::AlignHCenter), int(Qt::AlignRight)}) {
        auto doc = document(Qt::white, {600, 300});
        auto box = drawBox(doc.get(), QPointF(40, 40), QPointF(400, 100), u"Short words\nand a second line"_s);
        box->setTextAlignment(alignment | Qt::AlignTop);
        box->commitChanges();
        const QRectF words = box->geometryPath().boundingRect();
        // read before another mark is taken (the wrapper follows the selection)
        const qreal boxLine = QFontMetricsF(box->font()).height();
        const auto lines = Traits::textBoxLines(textOf(box), words.width());
        auto label = clickLabel(doc.get(), QPointF(300, 180), u"A much longer first line\nshort"_s);
        label->setTextAlignment(alignment | Qt::AlignTop);
        label->commitChanges();
        const QRectF labelWords = label->geometryPath().boundingRect();
        const qreal lineHeight = QFontMetricsF(label->font()).height();
        doc->deselectItem();
        const QImage image = doc->renderToImage();
        save(image, u"align-%1"_s.arg(alignment));
        const auto [left, right] = inkColumns(image, QRectF(words.left(), words.top(), words.width(), boxLine - 2).toAlignedRect());
        QVERIFY2(left >= 0, "the box's words are painted");
        // its second paragraph is painted on the line under the first (a block's place counted once)
        QCOMPARE(lines.size(), 2);
        QVERIFY2(inkColumns(image, QRectF(words.left(), words.top() + lines[1].second + 1, words.width(), boxLine - 2).toAlignedRect()).first >= 0, "the box's second line is painted in its place");
        QVERIFY2(inkColumns(image, QRectF(words.left(), words.top() + 2 * boxLine + 1, words.width(), boxLine).toAlignedRect()).first < 0, "nothing is painted below the box's second line");
        const QString what = u"aligned %1"_s.arg(alignment);
        if (alignment == Qt::AlignLeft) {
            QVERIFY2(left - words.left() < 3, qPrintable(what + u": words start %1 px in"_s.arg(left - words.left())));
        } else if (alignment == Qt::AlignRight) {
            QVERIFY2(words.right() - right < 3, qPrintable(what + u": words end %1 px short"_s.arg(words.right() - right)));
        } else {
            QVERIFY2(std::abs((left + right) / 2.0 - words.center().x()) < 3, qPrintable(what + u": words centered at %1, the box at %2"_s.arg((left + right) / 2.0).arg(words.center().x())));
        }
        // the label's second line
        const QRect second = QRectF(labelWords.left(), labelWords.top() + lineHeight + 1, labelWords.width(), lineHeight - 2).toAlignedRect();
        const auto [sLeft, sRight] = inkColumns(image, second);
        QVERIFY2(sLeft >= 0, "the label's second line is painted");
        if (alignment == Qt::AlignLeft) {
            QVERIFY2(sLeft - labelWords.left() < 3, qPrintable(what + u": the second line starts %1 px in"_s.arg(sLeft - labelWords.left())));
        } else if (alignment == Qt::AlignRight) {
            QVERIFY2(labelWords.right() - sRight < 3, qPrintable(what + u": the second line ends %1 px short"_s.arg(labelWords.right() - sRight)));
        } else {
            QVERIFY2(std::abs((sLeft + sRight) / 2.0 - labelWords.center().x()) < 3, qPrintable(what + u": the second line centered at %1, the label at %2"_s.arg((sLeft + sRight) / 2.0).arg(labelWords.center().x())));
        }
    }
}

void AnnotationLookTest::toolAlignmentCarriesOver()
{
    // the Text tool's alignment is the next text's, and stays the Text tool's when another tool is used
    auto doc = document(Qt::white, {400, 300});
    auto tool = doc->tool();
    tool->setType(AnnotationTool::TextTool);
    QCOMPARE(tool->textAlignment(), int(Qt::AlignLeft | Qt::AlignTop));
    tool->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    auto item = clickLabel(doc.get(), QPointF(200, 40), u"centered"_s);
    QCOMPARE(item->textAlignment(), int(Qt::AlignHCenter | Qt::AlignBottom));
    tool->setType(AnnotationTool::NumberTool);
    QCOMPARE(tool->textAlignment(), int(Qt::AlignLeft | Qt::AlignTop));
    tool->setType(AnnotationTool::TextTool);
    QCOMPARE(tool->textAlignment(), int(Qt::AlignHCenter | Qt::AlignBottom));
    // an alignment that is not one across and one up and down is refused, by the tool and by a mark
    for (const int wrong : {int(Qt::AlignJustify | Qt::AlignTop), int(Qt::AlignHCenter), int(Qt::AlignLeft | Qt::AlignRight | Qt::AlignTop), int(Qt::AlignLeft | Qt::AlignBaseline)}) {
        tool->setTextAlignment(wrong);
        QCOMPARE(tool->textAlignment(), int(Qt::AlignHCenter | Qt::AlignBottom));
        item->setTextAlignment(wrong);
        QCOMPARE(item->textAlignment(), int(Qt::AlignHCenter | Qt::AlignBottom));
    }
    tool->resetTextAlignment();
    QCOMPARE(tool->textAlignment(), int(Qt::AlignLeft | Qt::AlignTop));
}

void AnnotationLookTest::verticalAlignmentInABox()
{
    // In a box drawn taller than its words, the words sit at its top, its middle or its bottom; a box
    // whose words fill it is unchanged by any of them, and a click's label always has its words at its top.
    for (const int vertical : {int(Qt::AlignTop), int(Qt::AlignVCenter), int(Qt::AlignBottom)}) {
        auto doc = document(Qt::white, {600, 400});
        auto box = drawBox(doc.get(), QPointF(40, 40), QPointF(400, 240), u"Two short\nlines"_s);
        box->setTextAlignment(Qt::AlignLeft | vertical);
        box->commitChanges();
        const QRectF words = box->geometryPath().boundingRect();
        const auto lines = Traits::textBoxLines(textOf(box), words.width());
        const qreal wordsHeight = Traits::textBoxWordsSize(textOf(box), words.width()).height();
        QCOMPARE(lines.size(), 2);
        doc->deselectItem();
        const QImage image = doc->renderToImage();
        save(image, u"vertical-%1"_s.arg(vertical));
        const auto rows = inkRows(image, words.toAlignedRect());
        QVERIFY2(!rows.isEmpty(), "the words are painted");
        const qreal room = words.height() - wordsHeight;
        QVERIFY2(room > 100, qPrintable(u"the box is taller than its words (%1 px of room)"_s.arg(room)));
        const qreal expectedTop = words.top() + (vertical == Qt::AlignBottom ? room : vertical == Qt::AlignVCenter ? room / 2 : 0);
        const QString what = u"aligned %1"_s.arg(vertical);
        // the ink starts inside its first line, which starts where the alignment puts it
        QVERIFY2(rows.first() >= expectedTop - 1 && rows.first() < expectedTop + lines[1].second,
                 qPrintable(what + u": ink starts at %1, the words at %2"_s.arg(rows.first()).arg(expectedTop)));
        QVERIFY2(rows.last() <= expectedTop + wordsHeight + 1, qPrintable(what + u": ink ends at %1, the words at %2"_s.arg(rows.last()).arg(expectedTop + wordsHeight)));
    }
    // a click's label: its words fill it, top or not
    auto doc = document(Qt::white, {600, 400});
    auto label = clickLabel(doc.get(), QPointF(40, 60), u"one line"_s);
    const QRectF before = label->geometryPath().boundingRect();
    label->setTextAlignment(Qt::AlignLeft | Qt::AlignBottom);
    QCOMPARE(label->geometryPath().boundingRect(), before);
    QCOMPARE(Traits::textWordsTopOffset(textOf(label), before), 0.0);
}

void AnnotationLookTest::labelGrowsFromItsAlignedSide()
{
    // A click's label starts with its aligned side at the click and grows from it as words are typed:
    // a left-aligned one to the right, a centered one both ways, a right-aligned one to the left (as
    // tldraw's text grows); a bigger font keeps that side in place, and a change of alignment moves nothing.
    for (const int horizontal : {int(Qt::AlignLeft), int(Qt::AlignHCenter), int(Qt::AlignRight)}) {
        auto doc = document(Qt::white, {800, 400});
        doc->tool()->setType(AnnotationTool::TextTool);
        doc->tool()->setTextAlignment(horizontal | Qt::AlignTop);
        const QPointF click(400, 100);
        auto item = clickLabel(doc.get(), click, u"a"_s);
        auto side = [&] {
            const QRectF r = item->geometryPath().boundingRect();
            return horizontal == Qt::AlignRight ? r.right() : horizontal == Qt::AlignHCenter ? r.center().x() : r.left();
        };
        const QString what = u"aligned %1"_s.arg(horizontal);
        QVERIFY2(std::abs(side() - click.x()) < 0.01, qPrintable(what + u": the label's aligned side is at %1, the click at %2"_s.arg(side()).arg(click.x())));
        for (const QString &words : {u"a few"_s, u"a few more words"_s, u"a few more words\nand a second, longer line"_s}) {
            item->setText(words);
            QVERIFY2(std::abs(side() - click.x()) < 0.01, qPrintable(what + u", \"%1\": the aligned side moved to %2"_s.arg(words).arg(side())));
        }
        QFont big = item->font();
        big.setPointSize(28);
        item->setFont(big);
        QVERIFY2(std::abs(side() - click.x()) < 0.01, qPrintable(what + u": a bigger font moved the aligned side to %1"_s.arg(side())));
        const QRectF before = item->geometryPath().boundingRect();
        for (const int other : {int(Qt::AlignLeft), int(Qt::AlignHCenter), int(Qt::AlignRight)}) {
            item->setTextAlignment(other | Qt::AlignTop);
            QCOMPARE(item->geometryPath().boundingRect(), before);
        }
        doc->tool()->resetTextAlignment();
    }
    // an alignment picked after the click and before the words: the label grows from the click all the same
    for (const int horizontal : {int(Qt::AlignHCenter), int(Qt::AlignRight), int(Qt::AlignLeft)}) {
        auto doc = document(Qt::white, {800, 400});
        doc->tool()->setType(AnnotationTool::TextTool);
        doc->tool()->setShadow(false);
        const QPointF click(400, 100);
        doc->beginItem(click);
        doc->finishItem();
        auto item = doc->selectedItemWrapper();
        // first the other way, then this way
        item->setTextAlignment((horizontal == Qt::AlignRight ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignTop);
        item->setTextAlignment(horizontal | Qt::AlignTop);
        item->setText(u"some words"_s);
        const QRectF r = item->geometryPath().boundingRect();
        const qreal side = horizontal == Qt::AlignRight ? r.right() : horizontal == Qt::AlignHCenter ? r.center().x() : r.left();
        QVERIFY2(std::abs(side - click.x()) < 0.01, qPrintable(u"aligned %1 after the click: the aligned side is at %2, the click at %3"_s.arg(horizontal).arg(side).arg(click.x())));
    }
}

void AnnotationLookTest::longWordsPaintToTheEnd()
{
    // A box holding more than Qt lays out at once (past 1000 characters it lays a document out in steps):
    // every line, the last paragraph's too, is painted.
    QString words;
    for (int i = 0; i < 12; ++i) {
        words += sentence + u' ' + sentence + (i < 11 ? u"\n"_s : QString());
    }
    QVERIFY(words.size() > 1500);
    auto doc = document(Qt::white, {900, 1400});
    auto box = drawBox(doc.get(), QPointF(20, 20), QPointF(880, 60), words);
    const QRectF rect = box->geometryPath().boundingRect();
    const auto lines = Traits::textBoxLines(textOf(box), rect.width());
    const qreal lineHeight = QFontMetricsF(box->font()).height();
    doc->deselectItem();
    const QImage image = doc->renderToImage();
    save(image, u"long"_s);
    QVERIFY(lines.size() >= 24);
    for (int i = 0; i < lines.size(); ++i) {
        const QRect band = QRectF(rect.left(), rect.top() + lines[i].second + 1, rect.width(), lineHeight - 2).toAlignedRect();
        QVERIFY2(!inkRows(image, band).isEmpty(), qPrintable(u"line %1 of %2 is painted"_s.arg(i + 1).arg(lines.size())));
    }
}

void AnnotationLookTest::shiftAndCtrlShapeTheBox()
{
    // Through the marking canvas, as a hand draws: Shift makes the box square (its longer side), Ctrl
    // draws it out from the press as its center, both together a square around the press, as the
    // rectangle does; without them the box goes from the press to the pointer.
    struct Case {
        Qt::KeyboardModifiers modifiers;
        QPointF to;
        QRectF expected;
    };
    const QPointF press(200, 200);
    const QList<Case> cases{{Qt::NoModifier, QPointF(320, 240), QRectF(200, 200, 120, 40)},
                            {Qt::ShiftModifier, QPointF(320, 240), QRectF(200, 200, 120, 120)},
                            {Qt::ShiftModifier, QPointF(160, 100), QRectF(100, 100, 100, 100)},
                            {Qt::ControlModifier, QPointF(260, 230), QRectF(140, 170, 120, 60)},
                            {Qt::ShiftModifier | Qt::ControlModifier, QPointF(260, 230), QRectF(140, 140, 120, 120)}};
    for (const auto &c : cases) {
        auto doc = document(Qt::white, {600, 500});
        doc->tool()->setType(AnnotationTool::TextTool);
        QQuickWindow window;
        AnnotationViewport viewport(window.contentItem());
        viewport.setDocument(doc.get());
        viewport.setSize(QSizeF(600, 500));
        auto send = [&](QEvent::Type type, QPointF at, Qt::MouseButtons buttons) {
            QMouseEvent event(type, at, at, at, Qt::LeftButton, buttons, c.modifiers);
            QCoreApplication::sendEvent(&viewport, &event);
        };
        send(QEvent::MouseButtonPress, press, Qt::LeftButton);
        for (int step = 1; step <= 4; ++step) {
            send(QEvent::MouseMove, press + (c.to - press) * step / 4.0, Qt::LeftButton);
        }
        send(QEvent::MouseButtonRelease, c.to, Qt::NoButton);
        auto item = doc->selectedItemWrapper();
        const QString what = u"%1 to %2,%3"_s.arg(int(c.modifiers)).arg(c.to.x()).arg(c.to.y());
        QVERIFY2(item->isTextBox(), qPrintable(what + u": a box"_s));
        const QRectF outside = item->textOutsideRect();
        QVERIFY2(std::abs(outside.left() - c.expected.left()) < 0.5 && std::abs(outside.top() - c.expected.top()) < 0.5
                     && std::abs(outside.width() - c.expected.width()) < 0.5 && std::abs(outside.height() - c.expected.height()) < 0.5,
                 qPrintable(what + u": the box is %1, %2 expected"_s.arg(QDebug::toString(outside), QDebug::toString(c.expected))));
    }
}

void AnnotationLookTest::labelPaddingTakesThePress()
{
    // While a click's label is being written, a press in its padding (the green around the words)
    // places the cursor in it, as in a box, instead of starting a new mark; a press past the label
    // still starts one. Sent to the window, so Qt Quick picks who takes it.
    auto doc = document(Qt::white, {600, 400});
    auto item = clickLabel(doc.get(), QPointF(100, 100), u"hello there"_s);
    LiveTextTool live(doc.get());
    QVERIFY2(live.textTool, qPrintable(live.error));
    QTRY_VERIFY(live.editor());
    QQuickItem *editor = live.editor();
    QTRY_COMPARE(editor->property("text").toString(), u"hello there"_s);
    editor->setProperty("cursorPosition", 5);
    const QRectF words = item->geometryPath().boundingRect();
    const QRectF outside = item->textOutsideRect();
    QVERIFY2(outside.left() < words.left() - 3, "the label has padding");
    auto click = [&](QPointF at) {
        QMouseEvent press(QEvent::MouseButtonPress, at, at, at, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&live.window, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, at, at, at, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&live.window, &release);
    };
    const int depth = doc->undoStackDepth();
    // in the left padding, level with the words: the cursor goes to the words' start
    click(QPointF((outside.left() + words.left()) / 2, words.center().y()));
    QCOMPARE(doc->undoStackDepth(), depth);
    QCOMPARE(doc->selectedItemWrapper()->text(), u"hello there"_s);
    QCOMPARE(editor->property("cursorPosition").toInt(), 0);
    // in the right padding: the end
    click(QPointF((outside.right() + words.right()) / 2, words.center().y()));
    QCOMPARE(doc->undoStackDepth(), depth);
    QCOMPARE(editor->property("cursorPosition").toInt(), 11);
    // well past the label: a new mark
    click(QPointF(outside.right() + 60, words.center().y()));
    QTRY_VERIFY2(doc->undoStackDepth() > depth || doc->selectedItemWrapper()->text().isEmpty(), "a press past the label starts a new mark");
}

void AnnotationLookTest::typingStaysQuickWithManyBoxes()
{
    // A picture with 60 boxes of three sentences each and 60 labels, all with shadows, as busy as marked pictures get:
    // each key typed into one box (its words laid out again and the marks around it painted again,
    // until the document says it has repainted) and a full repaint (as a save makes) must stay well
    // inside one 60 Hz frame (16.7 ms): the middle key under 8 ms and 19 keys in 20 under 16.7, in this
    // thread's own working time. Prints what it measured.
    // a 1080p picture, and a 4K one at twice the pixels per point (a HiDPI screen's capture)
    for (const qreal dpr : {1.0, 2.0}) {
    auto doc = std::make_unique<AnnotationDocument>();
    {
        QImage base(QSize(1920, 1080) * dpr, QImage::Format_ARGB32_Premultiplied);
        base.fill(Qt::white);
        base.setDevicePixelRatio(dpr);
        doc->setBaseImage(base);
    }
    const QString words = sentence + u' ' + sentence + u' ' + sentence;
    for (int row = 0; row < 6; ++row) {
        for (int col = 0; col < 10; ++col) {
            const QPointF at(20 + col * 190, 20 + row * 175);
            // with their shadows, as text marks start
            auto item = drawBox(doc.get(), at, at + QPointF(170, 90), words);
            item->setShadow(true);
            item->commitChanges();
            doc->deselectItem();
            item = clickLabel(doc.get(), at + QPointF(0, 140), u"Label %1"_s.arg(row * 10 + col));
            item->setShadow(true);
            item->commitChanges();
            doc->deselectItem();
        }
    }
    // the box in the middle, taken again as a person clicks it to type
    doc->selectItem(QRectF(20 + 5 * 190 + 60, 20 + 3 * 175 + 30, 1, 1));
    auto edited = doc->selectedItemWrapper();
    QVERIFY(edited->isTextBox());
    QVERIFY(edited->hasShadow());
    QSignalSpy repainted(doc.get(), &AnnotationDocument::repainted);
    // let anything queued settle
    QTest::qWait(50);
    // the time this thread spends working (the marks are painted on it), which other programs on a busy
    // computer can't stretch the way they stretch the wall clock
    auto cpuNs = [] {
        timespec t{};
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
        return qint64(t.tv_sec) * 1000000000 + t.tv_nsec;
    };
    QList<qint64> keys;
    QString text = edited->text();
    for (const QChar c : u"and then some more words typed one key at a time"_s) {
        repainted.clear();
        const qint64 began = cpuNs();
        text += c;
        edited->setText(text);
        QVERIFY(repainted.wait(1000));
        keys << cpuNs() - began;
    }
    std::sort(keys.begin(), keys.end());
    const double median = keys[keys.size() / 2] / 1e6;
    const double worst = keys[keys.size() * 95 / 100] / 1e6;
    doc->deselectItem();
    const qint64 fullBegan = cpuNs();
    const QImage image = doc->renderToImage();
    const double fullMs = (cpuNs() - fullBegan) / 1e6;
    QVERIFY(!image.isNull());
    save(image, u"busy-%1x"_s.arg(dpr));
    QCOMPARE(image.size(), QSize(1920, 1080) * dpr);
    qInfo().noquote() << u"ECTOLENS-TYPING dpr=%1 keys=%2 median=%3ms p95=%4ms full-repaint=%5ms marks=120"_s.arg(dpr).arg(keys.size()).arg(median, 0, 'f', 2).arg(worst, 0, 'f', 2).arg(fullMs, 0, 'f', 2);
    QVERIFY2(median < 8, qPrintable(u"%1x: a key takes %2 ms to show"_s.arg(dpr).arg(median)));
    QVERIFY2(worst < 16.7, qPrintable(u"%1x: one key in twenty took %2 ms or more"_s.arg(dpr).arg(worst)));
    QVERIFY2(fullMs < 100, qPrintable(u"%1x: a full repaint takes %2 ms"_s.arg(dpr).arg(fullMs)));
    }
}

namespace
{
// a key as Qt sends it to the item with focus: first the shortcut check, then, if the item claims it, the key
void sendKey(QQuickItem *item, int key, Qt::KeyboardModifiers modifiers, const QString &text = QString())
{
    QKeyEvent override(QEvent::ShortcutOverride, key, modifiers, text);
    QCoreApplication::sendEvent(item, &override);
    if (!override.isAccepted() && !text.isEmpty()) {
        // plain typing is no shortcut
        override.setAccepted(true);
    }
    if (override.isAccepted()) {
        QKeyEvent press(QEvent::KeyPress, key, modifiers, text);
        QCoreApplication::sendEvent(item, &press);
        QKeyEvent release(QEvent::KeyRelease, key, modifiers, text);
        QCoreApplication::sendEvent(item, &release);
    }
}
}

void AnnotationLookTest::undoFollowsWhatHappenedLast()
{
    // While a box is being written in, Ctrl+Z takes back the picture's latest step, typing or not, and
    // Ctrl+Shift+Z puts it back (the editing box once took the typing back instead, and the picture's
    // history was out of reach until the box was left). Each case starts from a box just drawn.
    struct Live {
        std::unique_ptr<AnnotationDocument> doc;
        std::unique_ptr<LiveTextTool> live;
        SelectedItemWrapper *item = nullptr;
        QQuickItem *editor = nullptr;
        int before = 0;
    };
    auto start = [this]() {
        Live l;
        l.doc = document(Qt::white, {800, 600});
        l.before = l.doc->undoStackDepth();
        l.doc->tool()->setType(AnnotationTool::TextTool);
        l.doc->tool()->setShadow(false);
        l.doc->beginItem(QPointF(40, 40));
        l.doc->continueItem(QPointF(140, 70));
        l.doc->continueItem(QPointF(240, 110));
        l.doc->finishItem();
        l.item = l.doc->selectedItemWrapper();
        l.live = std::make_unique<LiveTextTool>(l.doc.get());
        return l;
    };
    auto type = [](QQuickItem *editor, const QString &words) {
        for (const QChar c : words) {
            sendKey(editor, c.toUpper().unicode(), Qt::NoModifier, QString(c));
        }
    };
    // the right handle dragged in, as a hand does: fixed points on the screen, each mapped to the
    // handle where it is then (it moves as the box narrows)
    auto drag = [](LiveTextTool &live, qreal by) {
        QQuickItem *area = live.find(u"textBoxHandle4"_s)->childItems().value(0);
        const QPointF pressed = area->mapToScene(QPointF(area->width() / 2, area->height() / 2));
        for (const auto &[kind, part, buttons] : {std::tuple{QEvent::MouseButtonPress, 0.0, Qt::MouseButtons(Qt::LeftButton)},
                                                  std::tuple{QEvent::MouseMove, 0.5, Qt::MouseButtons(Qt::LeftButton)},
                                                  std::tuple{QEvent::MouseButtonRelease, 1.0, Qt::MouseButtons(Qt::NoButton)}}) {
            const QPointF scene = pressed + QPointF(by * part, 0);
            QMouseEvent event(kind, area->mapFromScene(scene), scene, scene, Qt::LeftButton, buttons, Qt::NoModifier);
            QCoreApplication::sendEvent(area, &event);
        }
    };
    auto undo = [](QQuickItem *editor) {
        sendKey(editor, Qt::Key_Z, Qt::ControlModifier);
    };
    auto redo = [](QQuickItem *editor) {
        sendKey(editor, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    };

    {
        // typing, a drag, then Ctrl+Z, Ctrl+Shift+Z, Ctrl+Z, Ctrl+Z, Ctrl+Shift+Z, Ctrl+Shift+Z
        Live l = start();
        QVERIFY2(l.live->textTool, qPrintable(l.live->error));
        QTRY_VERIFY(l.live->editor());
        l.editor = l.live->editor();
        type(l.editor, u"note"_s);
        QTest::qWait(400);
        QCOMPARE(l.item->text(), u"note"_s);
        const QRectF drawn = l.item->textOutsideRect();
        QTRY_VERIFY(l.live->find(u"textBoxHandle4"_s));
        drag(*l.live, -60);
        const QRectF narrowed = l.item->textOutsideRect();
        QVERIFY2(std::abs(narrowed.width() - (drawn.width() - 60)) < 1, qPrintable(u"the handle narrowed the box to %1"_s.arg(narrowed.width())));
        undo(l.editor);
        QCOMPARE(l.item->textOutsideRect(), drawn);
        QCOMPARE(l.item->text(), u"note"_s);
        redo(l.editor);
        QCOMPARE(l.item->textOutsideRect(), narrowed);
        QCOMPARE(l.item->text(), u"note"_s);
        undo(l.editor);
        QCOMPARE(l.item->textOutsideRect(), drawn);
        // the words, typed in one go, are one step: the box goes with them, no first letter left behind
        undo(l.editor);
        QVERIFY2(!l.doc->selectedItemWrapper()->hasSelection(), "the second Ctrl+Z takes the typing back, and the box with it");
        QCOMPARE(l.doc->undoStackDepth(), l.before);
        l.doc->redo();
        l.doc->redo();
        // back, as narrowed (redo brings a mark back unselected; take it as a click does)
        l.doc->selectItem(QRectF(narrowed.center(), QSizeF(1, 1)));
        QVERIFY(l.doc->selectedItemWrapper()->hasSelection());
        QCOMPARE(l.doc->selectedItemWrapper()->text(), u"note"_s);
        QCOMPARE(l.doc->selectedItemWrapper()->textOutsideRect(), narrowed);
    }
    {
        // typing on both sides of a drag: Ctrl+Z takes back the later typing, then the drag, then the first typing
        Live l = start();
        QTRY_VERIFY(l.live->editor());
        l.editor = l.live->editor();
        type(l.editor, u"abc"_s);
        QTest::qWait(400);
        const QRectF drawn = l.item->textOutsideRect();
        QTRY_VERIFY(l.live->find(u"textBoxHandle4"_s));
        drag(*l.live, -60);
        const QRectF narrowed = l.item->textOutsideRect();
        type(l.editor, u"de"_s);
        QTest::qWait(400);
        QCOMPARE(l.item->text(), u"abcde"_s);
        undo(l.editor);
        QCOMPARE(l.item->text(), u"abc"_s);
        QCOMPARE(l.item->textOutsideRect(), narrowed);
        undo(l.editor);
        QCOMPARE(l.item->text(), u"abc"_s);
        QCOMPARE(l.item->textOutsideRect(), drawn);
        undo(l.editor);
        QVERIFY(!l.doc->selectedItemWrapper()->hasSelection());
    }
    {
        // a drag right after typing, before the typing was saved: every Ctrl+Z still takes a step back
        Live l = start();
        QTRY_VERIFY(l.live->editor());
        l.editor = l.live->editor();
        type(l.editor, u"ab"_s);
        const QRectF drawn = l.item->textOutsideRect();
        QTRY_VERIFY(l.live->find(u"textBoxHandle4"_s));
        drag(*l.live, -60);
        QCOMPARE(l.item->text(), u"ab"_s);
        undo(l.editor);
        QCOMPARE(l.item->textOutsideRect(), drawn);
        // the drag alone is taken back: the words typed just before it stay
        QCOMPARE(l.item->text(), u"ab"_s);
        undo(l.editor);
        QVERIFY2(!l.doc->selectedItemWrapper()->hasSelection(), "the second Ctrl+Z takes the box's first words back");
        QCOMPARE(l.doc->undoStackDepth(), l.before);
    }
    {
        // Ctrl+Z before the typing was saved: the words typed so far go in one step, first letter too
        Live l = start();
        QTRY_VERIFY(l.live->editor());
        l.editor = l.live->editor();
        type(l.editor, u"hello"_s);
        undo(l.editor);
        QVERIFY2(!l.doc->selectedItemWrapper()->hasSelection(), qPrintable(u"left: \"%1\""_s.arg(l.doc->selectedItemWrapper()->text())));
        QCOMPARE(l.doc->undoStackDepth(), l.before);
    }
    {
        // a new box's words show on the picture as they're typed, before any pause (the picture paints a
        // mark from its saved step, so its first letter is saved at once), and go into a save right away
        Live l = start();
        QTRY_VERIFY(l.live->editor());
        l.editor = l.live->editor();
        type(l.editor, u"Wide"_s);
        const QRectF words = l.item->geometryPath().boundingRect();
        const QImage now = l.doc->renderToImage();
        QVERIFY2(!inkRows(now, words.toAlignedRect()).isEmpty(), "the words show at once");
    }
    for (const int change : {0, 1, 2}) {
        // typing, then the picture cropped, turned or its colors changed at once: the words keep their place
        // in the history, so Ctrl+Z takes back that change first and the words stay
        Live l = start();
        QTRY_VERIFY(l.live->editor());
        l.editor = l.live->editor();
        type(l.editor, u"quick"_s);
        const QRectF canvas = l.doc->canvasRect();
        auto corner = [&] {
            // once the picture has been redrawn (its repaints wait for the event loop)
            QTest::qWait(50);
            return l.doc->canvasBaseImage().pixelColor(5, 5);
        };
        const QColor white = corner();
        if (change == 0) {
            l.doc->cropCanvas(QRectF(0, 0, 600, 500));
        } else if (change == 1) {
            QMatrix4x4 turn;
            turn.rotate(90, 0, 0, 1);
            l.doc->applyTransform(turn);
        } else {
            // every color darkened to a fifth
            QMatrix4x4 darker;
            darker.scale(0.2f, 0.2f, 0.2f);
            l.doc->applyColorAdjustment(darker);
        }
        QVERIFY(l.doc->canvasRect() != canvas || corner() != white);
        l.doc->deselectItem();
        l.doc->undo();
        QCOMPARE(l.doc->canvasRect(), canvas);
        QCOMPARE(corner(), white);
        l.doc->tool()->setType(AnnotationTool::SelectTool);
        l.doc->selectItem(QRectF(QPointF(140, 75), QSizeF(1, 1)));
        QVERIFY2(l.doc->selectedItemWrapper()->hasSelection(), qPrintable(u"the box is there after change %1 is undone"_s.arg(change)));
        QCOMPARE(l.doc->selectedItemWrapper()->text(), u"quick"_s);
        l.doc->tool()->setType(AnnotationTool::TextTool);
    }
    {
        // typing, then the mark let go at once (another click): the words stay
        Live l = start();
        QTRY_VERIFY(l.live->editor());
        l.editor = l.live->editor();
        type(l.editor, u"quick"_s);
        const QRectF outside = l.item->textOutsideRect();
        l.doc->deselectItem();
        QTest::qWait(400);
        l.doc->selectItem(QRectF(outside.center(), QSizeF(1, 1)));
        QVERIFY(l.doc->selectedItemWrapper()->hasSelection());
        QCOMPARE(l.doc->selectedItemWrapper()->text(), u"quick"_s);
    }
}

QTEST_MAIN(AnnotationLookTest)

#include "AnnotationLookTest.moc"
