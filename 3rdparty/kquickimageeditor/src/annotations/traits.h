/* SPDX-FileCopyrightText: 2024 Noah Davis <noahadvs@gmail.com>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include <QBrush>
#include <QColorSpace>
#include <QColorTransform>
#include <QFont>
#include <QHash>
#include <QMatrix4x4>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QUuid>

#include <optional>
#include <tuple>

namespace Traits
{
struct Geometry;
struct Interactive;
struct Visual;
struct Stroke;
namespace ImageEffects
{
class Blur;
class Pixelate;
}
struct Fill;
struct Highlight;
struct Arrow;
struct Text;
struct Shadow;
namespace Meta
{
struct Delete;
struct Crop;
struct Transform;
struct ColorAdjustment;
}

using OptTuple = std::tuple<std::optional<Geometry>,
                            std::optional<Interactive>,
                            std::optional<Visual>,
                            std::optional<Stroke>,
                            std::optional<Fill>,
                            std::optional<Highlight>,
                            std::optional<Arrow>,
                            std::optional<Text>,
                            std::optional<Shadow>,
                            std::optional<Meta::Delete>,
                            std::optional<Meta::Crop>,
                            std::optional<Meta::Transform>,
                            std::optional<Meta::ColorAdjustment>>;

struct Translation {
    // QTransform: m31
    // QMatrix4x4: 3,0 or m41
    qreal dx = 0;
    // QTransform: m32
    // QMatrix4x4: 3,1 or m42
    qreal dy = 0;
};

struct Scale {
    // QTransform: m11
    // QMatrix4x4: 0,0 or m11
    qreal sx = 1;
    // QTransform: m22
    // QMatrix4x4: 1,1 or m22
    qreal sy = 1;
};

// Undo a translation caused by scaling.
// Scaling can also translate unless you apply an opposite translation.
// `oldPoint` should be the position for the geometry you will apply the scale to.
Translation unTranslateScale(qreal sx, qreal sy, const QPointF &oldPoint);

Scale scaleForSize(const QSizeF &oldSize, const QSizeF &newSize);

// The path, but with at least a tiny line to make it visible with a stroke when empty.
QPainterPath minPath(const QPainterPath &path);

// Get an arrow head for the given line and stroke width.
QPainterPath arrowHead(const QLineF &mainLine, qreal strokeWidth);

// Get the path based on the Text trait.
QPainterPath createTextPath(const OptTuple &traits);

// Get the stroke path based on the available traits.
QPainterPath createStrokePath(const OptTuple &traits, const QPen *otherPen = nullptr);

// Constructs a mousePath based on the available traits.
QPainterPath createInteractivePath(const OptTuple &traits);

// Constructs a visualRect based on the available traits.
QRectF createVisualRect(const OptTuple &traits);

// Excludes computationally expensive parts that aren't needed for rendering
void fastInitOptTuple(OptTuple &traits);

// Initialize an OptTuple the way a HistoryItem should have it.
// Returns true if changes were done.
void initOptTuple(OptTuple &traits);

// Clear the given traits in the OptTuple for reinitialization.
void clearForInit(OptTuple &traits);

// clearForInit and initOptTuple
void reInitTraits(OptTuple &traits);

// Apply a transformation to the traits of OptTuple, when possible. Mainly for translations.
// If you want to scale a path, you typically don't want to scale the stroke width, so you should
// typically not use this for scaling. Avoids copying whole paths when only translating.
void transformTraits(const QTransform &transform, OptTuple &traits);

// Whether the std::optionals are considered valid.
template<typename T>
bool isValidTraitOpt(const OptTuple &traits, bool isNullValid);

// Whether the traits are considered valid.
// It is valid to not have any traits.
bool isValid(const OptTuple &traits);

// Returns whether the set of traits can actually be seen by a user.
bool isVisible(const OptTuple &traits);

// Returns whether the set of traits can be made visible even if they currently aren't.
bool canBeVisible(const OptTuple &traits);

// Returns the Geometry::path or an empty path if not available.
QPainterPath geometryPath(const OptTuple &traits);

// Returns the Geometry::path::boundingRect or an empty rect if not available.
QRectF geometryPathBounds(const OptTuple &traits);

// Returns the Interactive::path or an empty path if not available.
QPainterPath interactivePath(const OptTuple &traits);

// Returns the Visual::rect or an empty rect if not available.
QRectF visualRect(const OptTuple &traits);

// Ectolens: a Text mark with a visible fill is a label, its words on a rounded box of the
// fill's color; a Number mark with a visible fill is a badge. Both carry a hairline ring one
// shade darker than the fill (it keeps a light fill's edge on a light picture) and their words
// take near-black or white, whichever reads on the fill.
bool isLabel(const OptTuple &traits);
// The room between a label's words and its edge, for a font: width is left and right, height top and bottom.
QSizeF textLabelPadding(const QFont &font);
// The label box's corner radius, for a font.
qreal textLabelRadius(const QFont &font);
// What the fill covers: a text label's box, otherwise Geometry::path.
QPainterPath fillPath(const OptTuple &traits);
// The ring around a label or badge, and the color its words take.
QColor labelRingColor(const QColor &fill);
QColor labelTextColor(const QColor &fill);
// The width of the ring, in document pixels.
constexpr qreal labelRingWidth = 1.0;

// Ectolens: text boxes, drawn by a drag with the Text tool, whose words wrap inside them.
// A drag with the Text tool that takes the pointer this far from the press, in the window's logical
// pixels (whatever the zoom; 14 physical pixels on a 1.75 scale screen), draws a box; one that
// stays closer is a click (AnnotationViewport::mouseMoveEvent).
constexpr qreal textBoxDragThreshold = 8.0;
// The smallest box, for a font: room for a few letters on one line, with the label's padding.
QSizeF textBoxMinimumSize(const QFont &font);
// How a box's words lie in a given width: the size of the lines. The same layout as the editing box
// (TextTool.qml's TextEdit, wrapMode Wrap), so the cursor sits on the painted words.
QSizeF textBoxWordsSize(const Text &text, qreal width);
// Where each of a box's lines starts in its words (a character index) and its top, in that width:
// what the editing box must match (tests/AnnotationLookTest.cpp holds them to Qt Quick's TextEdit).
QList<std::pair<int, qreal>> textBoxLines(const Text &text, qreal width);
// Where the cursor stands before each of a Text mark's characters (and after the last) in its words
// rect laid out at a width: the x and the top of its line, as the editing box's positionToRectangle
// gives them (QQuickTextControlPrivate::rectForPosition). tests/AnnotationLookTest.cpp holds the two together.
// height: the words rect's height, which a box's words sit in by their vertical alignment.
QList<QPointF> textCursorPoints(const Text &text, qreal width, qreal height);
// The font a Text mark's words are laid out in: its own, at Qt Quick's 0.5 pt resolution (the
// editing box rounds a font's size so), so the mark's size and its painted words match the editing box.
QFont wordsFont(const QFont &font);
// How far down a box's words sit in their words rect (its vertical alignment), as the editing box
// offsets them (QQuickTextUtil::alignedY); 0 for a mark that is no box.
qreal textWordsTopOffset(const Text &text, const QRectF &wordsRect);
// After a label sized to its words changed size (its words or font), keeps the side it is aligned to
// where it was: its left, its center or its right (wordsBefore is its words rect before). Not a box.
void keepTextAnchor(OptTuple &traits, const QRectF &wordsBefore);
// Paints a Text mark's words into its words rect, as the editing box lays them out: wrapped at the
// rect's width for a box, one line per line otherwise, each line placed by the mark's alignment and
// direction. A number is painted centered (QPainter::drawText). The painter's pen is the words' color.
void drawTextWords(QPainter &painter, const Text &text, const QRectF &wordsRect);
// Where the words start in a box's outside rect (inside the label's padding), and a Text mark's
// outside rect for its words rect (the label's box: the words with the padding around them).
QPointF textBoxWordsTopLeft(const QFont &font, const QRectF &outside);
QRectF textOutsideRect(const QFont &font, const QRectF &wordsRect);

// Definitions all traits should have.
// Sometimes clang-format formats macros poorly, so I'm disabling clang-format on macros.
// clang-format off

#define COMMON_TRAIT_DEFS(ClassName)\
using Opt = std::optional<ClassName>;\
bool operator==(const ClassName &other) const = default;\
bool isValid() const;

// clang-format on

/* Not using QPixmap for images because I read from a Qt Company employee that QPixmaps are
 * almost always basically just QImages since Qt 5.
 *
 * https://www.reddit.com/r/QtFramework/comments/d9m17b/the_detailed_differences_between_qimage_and/f23m64t/
 *
 * nezitcle (Qt Company):
 *
 * > So it may be important to point out, but in Qt 5 QPixmap is almost always a QImage. This was
 * > important in Qt 4 when we had multiple paint engines and then QPixmap was the "native" image
 * > format for the platform you were running on. There is only one case I can think of where
 * > QPixmap is not a QImage and that is when you are using the DirectFB platform plugin (which is
 * > probably busted at this point, and definitely legacy code). There QPixmap is a bona fide native
 * > surface (DFBSurface), but asside from that there is no real reason to use QPixmap over QImage
 * > in Qt 5, aside from signaling to the user of an API that they shouldn't modify the pixel data
 * > directly.
 */

/**
 * Structs for each trait.
 *
 * These should all be kept pretty small and inexpensive to construct with minimal API.
 *
 * These should describe attributes of an abstract drawable object, not drawing tool object type.
 */

struct Geometry {
    COMMON_TRAIT_DEFS(Geometry)
    // The base geometry path for any item that should be drawable.
    QPainterPath path{};
};

struct Interactive {
    COMMON_TRAIT_DEFS(Interactive)
    // The the path bounds used for mouse interaction.
    QPainterPath path{};
};

struct Visual {
    COMMON_TRAIT_DEFS(Visual)
    // The rendered area, including all traits.
    // Adequate for rendering the associated item,
    // but not for overwriting the graphics of previous items.
    QRectF rect{};
};

struct Stroke {
    COMMON_TRAIT_DEFS(Stroke)
    // The pen to use for Stroke::path.
    // Use pen.brush() with QPainter::setBrush instead of setPen when rendering Stroke::path.
    static QPen defaultPen();
    QPen pen = defaultPen();
    // Used for rendering, getting the visual bounds and mouse interaction areas.
    QPainterPath path{};
};

namespace ImageEffects
{
class Blur
{
public:
    Blur(qreal);

    // Strength from 0 (minimum) to 1 (maximum).
    qreal strength() const;
    void setStrength(qreal);

    // Get an image that can be immediately used for rendering an image effect.
    // `getImage` should be the function used to generate the original image with no effects.
    // `rect` should be the section of the document you want to render over .
    // `dpr` should be the devicePixelRatio of the original image.
    QImage image(const std::function<QImage()> &getImage, const QRectF &rect, qreal dpr) const;

    bool operator==(const Blur &other) const = default;

private:
    // Setting as mutable means it can be mutated even when this is const
    // or using a const member function.
    mutable QImage m_backingStoreCache{};
    qreal m_strength = 0;
};

class Pixelate
{
public:
    Pixelate(qreal);

    // Strength from 0 (minimum) to 1 (maximum).
    qreal strength() const;
    void setStrength(qreal);

    // Get an image that can be immediately used for rendering an image effect.
    // `getImage` should be the function used to generate the original image with no effects.
    // `rect` should be the section of the document you want to render over .
    // `dpr` should be the devicePixelRatio of the original image.
    QImage image(const std::function<QImage()> &getImage, const QRectF &rect, qreal dpr) const;

    bool operator==(const Pixelate &other) const = default;

private:
    mutable QImage m_backingStoreCache{};
    qreal m_strength = 0;
};
}

struct Fill : public std::variant<QBrush, ImageEffects::Blur, ImageEffects::Pixelate> {
    COMMON_TRAIT_DEFS(Fill)
    enum Type : std::size_t {
        Brush,
        Blur,
        Pixelate,
        NPos = std::variant_npos
    };
};

struct Highlight {
    COMMON_TRAIT_DEFS(Highlight)
    // The QPainter::CompositionMode to use for highlight effects
    static constexpr QPainter::CompositionMode compositionMode = QPainter::CompositionMode_Darken;
};

struct Arrow {
    COMMON_TRAIT_DEFS(Arrow)
    // TODO: support different styles
    enum Type {
        OneHead
    };
};

// Set Geometry::path based on the bounding rect of this text.
struct Text : public std::variant<QString, int> {
    COMMON_TRAIT_DEFS(Text)
    enum Type : std::size_t {
        String,
        Number,
        NPos = std::variant_npos
    };
    int textFlags() const;
    // Set Geometry::path to the bounding rect of this text.
    QBrush brush{Qt::NoBrush};
    QFont font{};
    // Ectolens: the box a Text mark was drawn as (click and drag with the Text tool), its outside
    // size, which is a label's box. Its words wrap inside it, and it grows down only so no words
    // are hidden. Empty: the mark is sized to its words, as a click makes it.
    QSizeF box{};
    // Ectolens: where the words sit: across their box or label, on the picture's own left and right
    // whatever the words' language (Qt::AlignLeft, Qt::AlignHCenter or Qt::AlignRight), and, in a box
    // taller than its words, up and down it (Qt::AlignTop, Qt::AlignVCenter or Qt::AlignBottom), as the
    // editing box's horizontalAlignment and verticalAlignment place them. A label sized to its words
    // grows from its aligned side as words are added (a centered one both ways, a right-aligned one to
    // the left), as tldraw's text does. Numbers are always centered.
    Qt::Alignment alignment = Qt::AlignLeft | Qt::AlignTop;
    QString text() const;
    bool isBox() const;
    // the across part of alignment (Qt::AlignLeft if it holds none of the three)
    Qt::Alignment horizontal() const;
    // the up and down part for a box (Qt::AlignTop if it holds none of the three); Qt::AlignTop otherwise
    Qt::Alignment vertical() const;
    // Ectolens: the words' direction as the editing box takes it (QQuickTextEdit::q_textChanged):
    // the direction of the first strong letter in the first line that has one; with none, the
    // input method's (left to right without an application).
    Qt::LayoutDirection direction() const;
    // the direction of the first strong letter alone: Qt::LayoutDirectionAuto when there is none
    // (the editing box swaps left and right only for words with a right-to-left letter)
    Qt::LayoutDirection lettersDirection() const;
};

// Scaled to fit Geometry::visualRect
struct Shadow {
    COMMON_TRAIT_DEFS(Shadow)
    // The radius from the edge of the base graphics to where the shadow ends.
    static constexpr qreal radius = 2;
    // The X axis offset of the shadow.
    static constexpr qreal xOffset = 2;
    // The Y axis offset of the shadow.
    static constexpr qreal yOffset = 2;
    // The margins to be added to the visualRect so that it contains the shadow.
    static constexpr QMarginsF margins{
        std::max(0.0, radius - xOffset),
        std::max(0.0, radius - yOffset),
        std::max(0.0, radius + xOffset),
        std::max(0.0, radius + xOffset),
    };
    bool enabled = true;
};

// Traits that represent a kind of change to the document, but can't draw anything directly.
namespace Meta
{
struct Delete {
    COMMON_TRAIT_DEFS(Delete)
};
struct Crop {
    COMMON_TRAIT_DEFS(Crop)
};
struct Transform {
    COMMON_TRAIT_DEFS(Transform)
    QMatrix4x4 matrix;
    operator QTransform() const
    {
        return matrix.toTransform();
    }
    operator const QMatrix4x4 &() const
    {
        return matrix;
    }
};
struct ColorAdjustment {
    COMMON_TRAIT_DEFS(ColorAdjustment)
    // Matrix for linear RGB color transformations.
    // The row order is red, green, blue, projection.
    QMatrix4x4 matrix;
    // A gamma adjustment to be applied in a linear colorspace
    float gamma = 1.0f;
};
}

#undef COMMON_TRAIT_DEFS

}

// clang-format off
#define DEBUG_DEF(ClassName)\
QDebug operator<<(QDebug debug, const ClassName &ref);
#define DEBUG_TRAIT_DEF(ClassName)\
QDebug operator<<(QDebug debug, const ClassName &ref);\
QDebug operator<<(QDebug debug, const ClassName::Opt &ref);
// clang-format on

DEBUG_TRAIT_DEF(Traits::Geometry)
DEBUG_TRAIT_DEF(Traits::Interactive)
DEBUG_TRAIT_DEF(Traits::Visual)
DEBUG_TRAIT_DEF(Traits::Stroke)
DEBUG_TRAIT_DEF(Traits::Fill)
DEBUG_TRAIT_DEF(Traits::Highlight)
DEBUG_TRAIT_DEF(Traits::Arrow)
DEBUG_TRAIT_DEF(Traits::Text)
DEBUG_TRAIT_DEF(Traits::Shadow)
DEBUG_TRAIT_DEF(Traits::Meta::Delete)
DEBUG_TRAIT_DEF(Traits::Meta::Crop)
DEBUG_TRAIT_DEF(Traits::Meta::Transform)
DEBUG_TRAIT_DEF(Traits::Meta::ColorAdjustment)

DEBUG_DEF(Traits::ImageEffects::Blur)
DEBUG_DEF(Traits::ImageEffects::Pixelate)

DEBUG_DEF(Traits::OptTuple)

#undef DEBUG_TRAIT_DEF
#undef DEBUG_DEF
