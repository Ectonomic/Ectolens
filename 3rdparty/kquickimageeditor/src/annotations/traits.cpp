/* SPDX-FileCopyrightText: 2022 Marco Martin <mart@kde.org>
 * SPDX-FileCopyrightText: 2024 Noah Davis <noahadvs@gmail.com>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "traits.h"

#include <atomic>
#include <cmath>

#include <QAbstractTextDocumentLayout>
#include <QGuiApplication>
#include <QInputMethod>
#include <QLocale>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QThread>

#include "coloradjustment.h"
#include "stackblur.h"
#include "utils.h"

using namespace Qt::StringLiterals;

// Whether the values of the traits without std::optional are considered valid.
bool Traits::Geometry::isValid() const
{
    return !path.isEmpty();
}
bool Traits::Interactive::isValid() const
{
    return !path.isEmpty();
}
bool Traits::Visual::isValid() const
{
    return !rect.isEmpty();
}
bool Traits::Stroke::isValid() const
{
    return !path.isEmpty();
}
bool Traits::Fill::isValid() const
{
    switch (index()) {
    case Fill::Brush:
        return std::get<Fill::Brush>(*this) != Qt::NoBrush;
    case Fill::Blur:
    case Fill::Pixelate:
        return true;
    default:
        return false;
    }
}
bool Traits::Highlight::isValid() const
{
    return true;
}
bool Traits::Arrow::isValid() const
{
    return true;
}
bool Traits::Text::isValid() const
{
    return brush != Qt::NoBrush //
        && (index() == Traits::Text::Number || !text().isEmpty());
}
bool Traits::Shadow::isValid() const
{
    return true;
}
bool Traits::Meta::Delete::isValid() const
{
    return true;
}
bool Traits::Meta::Crop::isValid() const
{
    return true;
}
bool Traits::Meta::Transform::isValid() const
{
    return true;
}
bool Traits::Meta::ColorAdjustment::isValid() const
{
    return !matrix.isIdentity() || ::ColorAdjustment::isValidGammaAdjustment(gamma);
}

// Stroke
QPen Traits::Stroke::defaultPen()
{
    return {Qt::NoBrush, 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin};
}

int Traits::Text::textFlags() const
{
    return (index() == Text::String ? Qt::AlignLeft | Qt::AlignTop : Qt::AlignCenter) //
        | Qt::TextDontClip | Qt::TextExpandTabs | Qt::TextIncludeTrailingSpaces;
}

bool Traits::Text::isBox() const
{
    return index() == String && box.width() > 0 && box.height() > 0;
}

Qt::Alignment Traits::Text::horizontal() const
{
    const Qt::Alignment h = alignment & (Qt::AlignLeft | Qt::AlignHCenter | Qt::AlignRight);
    return h == Qt::AlignHCenter || h == Qt::AlignRight ? h : Qt::AlignLeft;
}

Qt::Alignment Traits::Text::vertical() const
{
    const Qt::Alignment v = alignment & (Qt::AlignTop | Qt::AlignVCenter | Qt::AlignBottom);
    return isBox() && (v == Qt::AlignVCenter || v == Qt::AlignBottom) ? v : Qt::AlignTop;
}

Qt::LayoutDirection Traits::Text::lettersDirection() const
{
    // as QQuickTextEditPrivate::textDirection, line by line until one has a strong letter
    const QStringList lines = text().split(u'\n');
    for (const QString &line : lines) {
        for (const QChar c : line) {
            switch (c.direction()) {
            case QChar::DirL:
                return Qt::LeftToRight;
            case QChar::DirR:
            case QChar::DirAL:
            case QChar::DirAN:
                return Qt::RightToLeft;
            default:
                break;
            }
        }
    }
    return Qt::LayoutDirectionAuto;
}

Qt::LayoutDirection Traits::Text::direction() const
{
    if (const auto letters = lettersDirection(); letters != Qt::LayoutDirectionAuto) {
        return letters;
    }
    // the input method lives on the main thread; marks may be painted on another (syncImages), which
    // takes the direction the main thread last read
    static std::atomic<int> lastInputDirection{Qt::LeftToRight};
    auto app = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
    if (app && QThread::currentThread() == app->thread() && app->inputMethod()) {
        lastInputDirection = app->inputMethod()->inputDirection();
    }
    return Qt::LayoutDirection(lastInputDirection.load());
}

QString Traits::Text::text() const
{
    if (index() == String) {
        return std::get<String>(*this);
    } else if (index() == Number) {
        return QLocale::system().toString(std::get<Number>(*this));
    }
    return {};
}

// ImageEffects

static const auto strengthKey = u"strength"_s;

static QString strengthString(qreal strength)
{
    return QString::number(strength, 'f', std::numeric_limits<qreal>::digits10);
}

#ifdef Q_OS_WIN
static qreal clampStrength(qreal strength)
#else
static constexpr qreal clampStrength(qreal strength)
#endif
{
    if (std::isnan(strength)) {
        return 0.0;
    } else if (std::isinf(strength)) {
        return 1.0;
    }
    return std::clamp(strength, 0.0, 1.0);
}

Traits::ImageEffects::Blur::Blur(qreal strength)
    : m_strength(strength)
{
}

qreal Traits::ImageEffects::Blur::strength() const
{
    return m_strength;
}

void Traits::ImageEffects::Blur::setStrength(qreal strength)
{
    strength = clampStrength(strength);
    if (m_strength == strength) {
        return;
    }
    m_strength = strength;
    m_backingStoreCache = {};
}

QImage Traits::ImageEffects::Blur::image(const std::function<QImage()> &getImage, const QRectF &rect, qreal dpr) const
{
    if (getImage && (m_backingStoreCache.isNull() || m_backingStoreCache.devicePixelRatio() != dpr)) {
        m_backingStoreCache = getImage();
        if (m_backingStoreCache.isNull()) {
            return m_backingStoreCache;
        }
        const auto min = StackBlur::minRadius();
        const auto max = StackBlur::maxRadius();
        // Scales with DPR to keep the effect looking similar for different image DPRs.
        const qreal dynamicMin = 1 * dpr;
        const qreal dynamicMax = 64 * dpr;
        const auto radius = qRound(Utils::clamp(m_strength * (dynamicMax - dynamicMin) + dynamicMin, min, max));
        StackBlur::blur(m_backingStoreCache, radius, radius);
        m_backingStoreCache.setDevicePixelRatio(dpr);
    }
    QRect copyRect = Utils::rectScaled(rect, m_backingStoreCache.devicePixelRatio()).toAlignedRect();
    if (copyRect.size() != m_backingStoreCache.size()) {
        return m_backingStoreCache.copy(copyRect);
    }
    return m_backingStoreCache;
}

Traits::ImageEffects::Pixelate::Pixelate(qreal strength)
    : m_strength(strength)
{
}

qreal Traits::ImageEffects::Pixelate::strength() const
{
    return m_strength;
}

void Traits::ImageEffects::Pixelate::setStrength(qreal strength)
{
    strength = clampStrength(strength);
    if (m_strength == strength) {
        return;
    }
    m_strength = strength;
    m_backingStoreCache = {};
}

QImage Traits::ImageEffects::Pixelate::image(const std::function<QImage()> &getImage, const QRectF &rect, qreal dpr) const
{
    if ((m_backingStoreCache.isNull() //
         || m_backingStoreCache.devicePixelRatio() != dpr //
         || m_backingStoreCache.text(strengthKey).toDouble() != m_strength)
        && getImage) {
        m_backingStoreCache = getImage();
        if (m_backingStoreCache.isNull()) {
            return m_backingStoreCache;
        }
        // 1x would have no effect and a fractional scale would look bad, so 2x is the minimum.
        static const qreal min = 2;
        // Scales with DPR to keep the effect looking similar for different image DPRs.
        const qreal dynamicMin = min * dpr;
        const qreal dynamicMax = 16 * dpr;
        const auto factor = std::max(std::round(m_strength * (dynamicMax - dynamicMin) + dynamicMin), min);
        auto scaleDown = QTransform::fromScale(1 / factor, 1 / factor);
        auto scaleUp = QTransform::fromScale(factor, factor);
        // Smooth when scaling down to average out the colors.
        m_backingStoreCache = m_backingStoreCache.transformed(scaleDown, Qt::SmoothTransformation);
        m_backingStoreCache = m_backingStoreCache.transformed(scaleUp, Qt::FastTransformation);
        m_backingStoreCache.setDevicePixelRatio(dpr);
        m_backingStoreCache.setText(strengthKey, strengthString(m_strength));
    }
    QRect copyRect = Utils::rectScaled(rect, m_backingStoreCache.devicePixelRatio()).toAlignedRect();
    if (copyRect.size() != m_backingStoreCache.size()) {
        return m_backingStoreCache.copy(copyRect);
    }
    return m_backingStoreCache;
}

// Functions

Traits::Translation Traits::unTranslateScale(qreal sx, qreal sy, const QPointF &oldPoint)
{
    return {-oldPoint.x() * sx + oldPoint.x(), -oldPoint.y() * sy + oldPoint.y()};
}

Traits::Scale Traits::scaleForSize(const QSizeF &oldSize, const QSizeF &newSize)
{
    // We should never divide by zero and we don't need fractional sizes less than 1.
    auto absWidth = std::abs(oldSize.width());
    auto absHeight = std::abs(oldSize.height());
    auto wSign = std::copysign(1.0, oldSize.width());
    auto hSign = std::copysign(1.0, oldSize.height());
    // Don't allow an absolute size less than 1x1.
    const auto wDivisor = std::max(1.0, absWidth) * wSign;
    const auto hDivisor = std::max(1.0, absHeight) * hSign;
    return {newSize.width() / wDivisor, newSize.height() / hDivisor};
}

QPainterPath Traits::minPath(const QPainterPath &path)
{
    if (path.isEmpty()) {
        auto start = path.elementCount() > 0 ? path.elementAt(0) : QPainterPath::Element{};
        QPainterPath dotPath(start);
        dotPath.lineTo(start.x + 0.0001, start.y);
        return dotPath;
    }
    return path;
}

QPainterPath Traits::arrowHead(const QLineF &mainLine, qreal strokeWidth)
{
    const auto &end = mainLine.p2();
    // This should leave a decently sized gap between the arrow head and shaft
    // and a decently sized length for all stroke widths.
    // Arrow head length will grow with stroke width.
    const qreal length = qMax(8.0, strokeWidth * 3.0);
    const qreal angle = mainLine.angle() + 180;
    auto headLine1 = QLineF::fromPolar(length, angle + 30).translated(end);
    auto headLine2 = QLineF::fromPolar(length, angle - 30).translated(end);
    QPainterPath path(headLine1.p2());
    path.lineTo(end);
    path.lineTo(headLine2.p2());
    return path;
}

QPainterPath Traits::createTextPath(const OptTuple &traits)
{
    auto &geometry = std::get<Geometry::Opt>(traits);
    auto &text = std::get<Text::Opt>(traits);
    if (!geometry) {
        return {};
    }
    if (!text) {
        return geometry->path;
    }
    const auto &start = geometry->path.elementCount() > 0 ? geometry->path.elementAt(0) : QPainterPath::Element{};
    QRectF rect{start, start};
    QFontMetricsF fm(text->font);
    QPainterPath path{start};
    if (text->isBox()) {
        // Ectolens: a drawn box. Its words wrap at its width inside the label's padding; it keeps
        // its drawn height and grows only so no words are hidden.
        const auto pad = textLabelPadding(text->font);
        // never narrower than the smallest box's words (two line heights), so a bigger font in a
        // small box doesn't stack its words a letter or two to a line; the box widens instead
        const qreal wordsWidth = std::max(textBoxMinimumSize(text->font).width() - 2 * pad.width(), text->box.width() - 2 * pad.width());
        const qreal wordsHeight = std::max(textBoxWordsSize(*text, wordsWidth).height(), text->box.height() - 2 * pad.height());
        rect = QRectF(QPointF(start.x, start.y - fm.height() / 2), QSizeF(wordsWidth, wordsHeight));
        path.addRect(rect);
    } else if (text->index() == Text::String) {
        // Ectolens: measured in the font the words are laid out in (its size at Qt Quick's 0.5 pt), with
        // the tab stop the editing box has; a label keeps its aligned side as it grows (keepTextAnchor),
        // and its words are placed by their own direction (wordsDocument)
        const QFontMetricsF wfm(wordsFont(text->font));
        const auto tabStopDistance = qRound(QFontMetricsF(text->font).horizontalAdvance(u'x') * 8);
        auto size = wfm.size(text->textFlags(), text->text(), tabStopDistance);
        size.rwidth() = std::max(size.width(), fm.height());
        size.rheight() = std::max(size.height(), fm.height());
        rect.adjust(0, -fm.height() / 2, size.width(), size.height() - fm.height() / 2);
        path.addRect(rect);
    } else if (text->index() == Text::Number) {
        auto margin = fm.capHeight() * 1.33;
        // Ectolens: a number wider than the circle gets a pill of the same height
        const qreal halfWidth = std::max(margin, (fm.horizontalAdvance(text->text()) + fm.capHeight() * 1.2) / 2);
        rect.adjust(-halfWidth, -margin, halfWidth, margin);
        if (halfWidth > margin) {
            path.addRoundedRect(rect, margin, margin);
        } else {
            path.addEllipse(rect);
        }
    }
    return path;
}

QPainterPath Traits::createStrokePath(const OptTuple &traits, const QPen *otherPen)
{
    auto &geometry = std::get<Geometry::Opt>(traits);
    auto &stroke = std::get<Stroke::Opt>(traits);
    if (!geometry && !stroke) {
        return {};
    }
    const auto &pen = otherPen ? *otherPen : stroke->pen;
    QPainterPathStroker stroker(pen);
    auto minPath = Traits::minPath(geometry->path); // Will always have at least 2 points.
    if (auto &arrow = std::get<Arrow::Opt>(traits)) {
        const int size = minPath.elementCount();
        const QLineF lastLine{minPath.elementAt(size - 2), minPath.elementAt(size - 1)};
        auto arrowHead = Traits::arrowHead(lastLine, pen.widthF());
        return stroker.createStroke(minPath) | stroker.createStroke(arrowHead);
    } else {
        return stroker.createStroke(minPath);
    }
}

QPainterPath Traits::createInteractivePath(const OptTuple &traits)
{
    auto &geometry = std::get<Geometry::Opt>(traits);
    auto &stroke = std::get<Stroke::Opt>(traits);
    QPainterPath mousePath;
    if (geometry && geometry->isValid()) {
        mousePath = geometry->path;
        // Ectolens: all of a label's box takes clicks, and all of a drawn text box with no label
        auto &text = std::get<Text::Opt>(traits);
        if (isLabel(traits)) {
            mousePath = fillPath(traits);
        } else if (text && text->isBox()) {
            mousePath = QPainterPath();
            mousePath.addRect(textOutsideRect(text->font, geometry->path.boundingRect()));
        }
    }
    // Ensure you can click anywhere within the bounds.
    mousePath.setFillRule(Qt::WindingFill);
    if (stroke && stroke->isValid()) {
        mousePath |= stroke->path;
    }
    // If it's somehow still empty, force it to be clickable.
    mousePath = Traits::minPath(mousePath);
    // Add an extra 1px all around. Each bounding rect corner moves out 0.5px.
    QPen pen{Qt::NoBrush, 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin};
    QPainterPathStroker stroker(pen);
    mousePath |= stroker.createStroke(mousePath);
    return mousePath.simplified();
}

QRectF Traits::createVisualRect(const OptTuple &traits)
{
    auto &geometry = std::get<Geometry::Opt>(traits);
    auto &stroke = std::get<Stroke::Opt>(traits);
    if (!geometry) {
        return {};
    }
    QRectF visualRect;
    if (stroke) {
        visualRect = stroke->path.boundingRect() | geometry->path.boundingRect();
    } else {
        visualRect = geometry->path.boundingRect();
    }
    // Ectolens: a label's box and the ring around a label or badge
    if (isLabel(traits)) {
        const qreal half = labelRingWidth / 2;
        visualRect |= fillPath(traits).boundingRect().adjusted(-half, -half, half, half);
    }
    // Add Shadow margins if not empty.
    auto &shadow = std::get<Shadow::Opt>(traits);
    if (shadow && shadow->enabled && !visualRect.isEmpty()) {
        visualRect += Shadow::margins;
    }
    return visualRect;
}

void Traits::fastInitOptTuple(OptTuple &traits)
{
    auto &geometry = std::get<Geometry::Opt>(traits);
    if (geometry) {
        // Set Geometry::path from Font and Text/Number if empty.
        auto &text = std::get<Text::Opt>(traits);
        if (geometry->path.isEmpty() && text) {
            geometry->path = Traits::createTextPath(traits);
        }
        // Set Stroke::path from Geometry and Arrow if empty.
        auto &stroke = std::get<Stroke::Opt>(traits);
        if (stroke && stroke->path.isEmpty()) {
            stroke->path = createStrokePath(traits);
        }
        // Set Visual::rect from Stroke and Geometry if empty.
        auto &visual = std::get<Visual::Opt>(traits);
        if (visual && visual->rect.isEmpty()) {
            visual->rect = createVisualRect(traits);
        }
    }
}

void Traits::initOptTuple(OptTuple &traits)
{
    fastInitOptTuple(traits);
    auto &interactive = std::get<Interactive::Opt>(traits);
    if (interactive && interactive->path.isEmpty()) {
        // Set Interactive::path from Stroke and Geometry if empty.
        interactive->path = createInteractivePath(traits);
    }
}

template<typename T>
void clearForInitHelper(Traits::OptTuple &traits)
{
    auto &traitOpt = std::get<std::optional<T>>(traits);
    if (!traitOpt) {
        return;
    }
    auto &trait = traitOpt.value();
    if constexpr (std::same_as<T, Traits::Interactive>) {
        trait.path.clear();
    } else if constexpr (std::same_as<T, Traits::Visual>) {
        trait.rect = {};
    } else if constexpr (std::same_as<T, Traits::Stroke>) {
        trait.path.clear();
    } else if constexpr (std::same_as<T, Traits::Text>) {
        auto &geometry = std::get<Traits::Geometry::Opt>(traits);
        if (!geometry) {
            return;
        }
        if (trait.index() == Traits::Text::String) {
            QFontMetricsF fm(trait.font);
            // (Ectolens: a label's aligned side is kept by keepTextAnchor, after this)
            QPointF topLeft;
            if (geometry->path.elementCount() == 1) {
                topLeft = geometry->path.elementAt(0);
            } else {
                topLeft = geometry->path.boundingRect().topLeft();
            }
            geometry->path = QPainterPath{topLeft + QPointF{0, fm.height() / 2}};
        } else if (trait.index() == Traits::Text::Number) {
            QPointF point;
            if (geometry->path.elementCount() == 1) {
                point = geometry->path.elementAt(0);
            } else {
                point = geometry->path.boundingRect().center();
            }
            geometry->path = QPainterPath{point};
        }
    }
}

void Traits::clearForInit(OptTuple &traits)
{
    clearForInitHelper<Interactive>(traits);
    clearForInitHelper<Visual>(traits);
    clearForInitHelper<Stroke>(traits);
    clearForInitHelper<Text>(traits);
}

void Traits::reInitTraits(OptTuple &traits)
{
    clearForInit(traits);
    initOptTuple(traits);
}

void Traits::transformTraits(const QTransform &transform, OptTuple &traits)
{
    if (transform.isIdentity()) {
        return;
    }
    auto &geometry = std::get<Geometry::Opt>(traits);
    auto &text = std::get<Text::Opt>(traits);
    bool onlyTranslating = transform.type() == QTransform::TxTranslate || text;
    if (geometry && onlyTranslating) {
        geometry->path.translate(transform.dx(), transform.dy());
    } else if (geometry) {
        geometry->path = transform.map(geometry->path);
    }
    auto &interactive = std::get<Interactive::Opt>(traits);
    if (interactive && onlyTranslating) {
        interactive->path.translate(transform.dx(), transform.dy());
    } else if (interactive) {
        interactive->path = transform.map(interactive->path);
    }
    auto &visual = std::get<Visual::Opt>(traits);
    if (visual && onlyTranslating) {
        // This is dependent on other traits, but as long as all traits have,
        // the same transformations, transforming at this time should be fine.
        visual->rect.translate(transform.dx(), transform.dy());
    } else if (geometry) {
        // This is dependent on other traits, but as long as all traits have,
        // the same transformations, transforming at this time should be fine.
        visual->rect = transform.mapRect(visual->rect);
    }
    auto &stroke = std::get<Stroke::Opt>(traits);
    if (stroke && onlyTranslating) {
        // If the stroke already has the arrow in it,
        // we shouldn't need to completely regenerate the stroke with QPainterPathStroker.
        stroke->path.translate(transform.dx(), transform.dy());
    } else if (stroke) {
        stroke->path = transform.map(stroke->path);
    }
}

// Whether the std::optionals are considered valid.
template<typename T>
bool Traits::isValidTraitOpt(const Traits::OptTuple &traits, bool isNullValid)
{
    auto &traitOpt = std::get<std::optional<T>>(traits);
    if (!traitOpt) {
        return isNullValid;
    }
    auto &trait = traitOpt.value();

    if constexpr (std::same_as<T, Traits::Geometry>) {
        return trait.isValid();
    }
    if constexpr (std::same_as<T, Traits::Interactive>) {
        return trait.isValid();
    }
    if constexpr (std::same_as<T, Traits::Visual>) {
        return trait.isValid();
    }
    if constexpr (std::same_as<T, Meta::Delete>) {
        return trait.isValid();
    }
    if constexpr (std::same_as<T, Meta::Transform>) {
        return trait.isValid();
    }
    if constexpr (std::same_as<T, Meta::ColorAdjustment>) {
        return trait.isValid();
    }

    // Traits that depend on geometry
    auto &geometry = std::get<Traits::Geometry::Opt>(traits);
    const bool validGeometry = geometry && geometry->isValid();
    if constexpr (std::same_as<T, Stroke>) {
        return validGeometry && trait.isValid();
    }
    if constexpr (std::same_as<T, Fill>) {
        return validGeometry && trait.isValid();
    }
    if constexpr (std::same_as<T, Text>) {
        return validGeometry && trait.isValid();
    }
    if constexpr (std::same_as<T, Meta::Crop>) {
        return validGeometry && trait.isValid();
    }

    // Traits that depend on vector graphic traits
    auto &stroke = std::get<Stroke::Opt>(traits);
    auto &fill = std::get<Fill::Opt>(traits);
    auto &text = std::get<Text::Opt>(traits);
    const bool validStroke = stroke && stroke->isValid();
    const bool validFill = fill && fill->isValid();
    const bool validText = text && text->isValid();
    if constexpr (std::same_as<T, Highlight>) {
        return validGeometry && (validStroke || validFill || validText) //
            && trait.isValid();
    }
    if constexpr (std::same_as<T, Arrow>) {
        return validGeometry && (validStroke || validFill || validText) //
            && trait.isValid();
    }
    if constexpr (std::same_as<T, Shadow>) {
        return validGeometry && (validStroke || validFill || validText) //
            && trait.isValid();
    }
    return false;
}

template<typename... Ts>
bool isValidHelper(const Traits::OptTuple &traits)
{
    return (Traits::isValidTraitOpt<Ts>(traits, true) && ...);
}

bool Traits::isValid(const OptTuple &traits)
{
    return isValidHelper<Geometry, Interactive, Visual, Stroke, Fill, Highlight, Arrow, Text, Shadow, Meta::Delete, Meta::Crop, Meta::Transform, Meta::ColorAdjustment>(traits);
}

bool Traits::isVisible(const OptTuple &traits)
{
    // Ectolens: a text mark is seen through its words, whatever its fill (its label's box)
    if (auto &text = std::get<Text::Opt>(traits); text && !text->isValid()) {
        return false;
    }
    return Traits::isValidTraitOpt<Visual>(traits, false) //
        && Traits::isValidTraitOpt<Geometry>(traits, false) //
        && (Traits::isValidTraitOpt<Stroke>(traits, false) //
            || Traits::isValidTraitOpt<Fill>(traits, false) //
            || Traits::isValidTraitOpt<Text>(traits, false));
}

bool Traits::canBeVisible(const OptTuple &traits)
{
    return std::get<Visual::Opt>(traits) //
        && std::get<Geometry::Opt>(traits) //
        && (std::get<Stroke::Opt>(traits) //
            || std::get<Fill::Opt>(traits) //
            || std::get<Text::Opt>(traits));
}

QPainterPath Traits::geometryPath(const OptTuple &traits)
{
    auto &geometry = std::get<Geometry::Opt>(traits);
    return geometry ? geometry->path : QPainterPath{};
}

QRectF Traits::geometryPathBounds(const OptTuple &traits)
{
    auto &geometry = std::get<Geometry::Opt>(traits);
    return geometry ? geometry->path.boundingRect() : QRectF{};
}

QPainterPath Traits::interactivePath(const OptTuple &traits)
{
    auto &interactive = std::get<Interactive::Opt>(traits);
    return interactive ? interactive->path : QPainterPath{};
}

bool Traits::isLabel(const OptTuple &traits)
{
    auto &text = std::get<Text::Opt>(traits);
    auto &fill = std::get<Fill::Opt>(traits);
    // a text mark with no words yet is no label (nothing to draw)
    if (!text || !text->isValid() || !fill || fill->index() != Fill::Brush) {
        return false;
    }
    return std::get<Fill::Brush>(fill.value()).color().alpha() > 0;
}

QSizeF Traits::textLabelPadding(const QFont &font)
{
    const QFontMetricsF fm(font);
    return {std::round(fm.height() * 0.48), std::round(fm.height() * 0.16)};
}

qreal Traits::textLabelRadius(const QFont &font)
{
    return std::round(QFontMetricsF(font).height() * 0.32);
}

QPainterPath Traits::fillPath(const OptTuple &traits)
{
    auto &geometry = std::get<Geometry::Opt>(traits);
    if (!geometry) {
        return {};
    }
    auto &text = std::get<Text::Opt>(traits);
    if (text && text->index() == Text::String && isLabel(traits)) {
        const auto radius = textLabelRadius(text->font);
        QPainterPath path;
        path.addRoundedRect(textOutsideRect(text->font, geometry->path.boundingRect()), radius, radius);
        return path;
    }
    return geometry->path;
}

QSizeF Traits::textBoxMinimumSize(const QFont &font)
{
    const QFontMetricsF fm(font);
    const auto pad = textLabelPadding(font);
    return {std::ceil(fm.height() * 2) + 2 * pad.width(), std::ceil(fm.height()) + 2 * pad.height()};
}

namespace
{
// A Text mark's words in a QTextDocument set up as the editing box sets up its own (Qt Quick's
// QQuickTextEdit, QQuickTextEditPrivate::updateDefaultTextOption: the mark's alignment, with left and
// right swapped for right-to-left words so the layout's own swap puts them back on the picture's
// left and right; top alignment; the words' direction; wrapping at word boundaries or anywhere for a
// box, none otherwise; native metrics, no margin; the font at its 0.5 pt resolution; the tab stop
// TextTool.qml sets) and laid out at the width the editing box has. Both then break and place lines
// in the same places.
std::unique_ptr<QTextDocument> wordsDocument(const Traits::Text &text, qreal width)
{
    auto document = std::make_unique<QTextDocument>();
    document->setDefaultFont(Traits::wordsFont(text.font));
    document->setDocumentMargin(0);
    const Qt::LayoutDirection direction = text.direction();
    Qt::Alignment horizontal = text.horizontal();
    // swapped only for words with a right-to-left letter, as the editing box swaps them (its
    // contentDirection); words with none take the input method's direction unswapped
    if (text.lettersDirection() == Qt::RightToLeft && horizontal != Qt::AlignHCenter) {
        horizontal = horizontal == Qt::AlignLeft ? Qt::AlignRight : Qt::AlignLeft;
    }
    QTextOption option = document->defaultTextOption();
    option.setAlignment(horizontal | Qt::AlignTop);
    option.setTextDirection(direction);
    option.setWrapMode(text.isBox() ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);
    option.setUseDesignMetrics(false);
    option.setTabStopDistance(qRound(QFontMetricsF(text.font).horizontalAdvance(u'x') * 8));
    document->setDefaultTextOption(option);
    document->setPlainText(text.text());
    document->setTextWidth(width);
    return document;
}
}

QSizeF Traits::textBoxWordsSize(const Text &text, qreal width)
{
    return wordsDocument(text, width)->size();
}

QList<std::pair<int, qreal>> Traits::textBoxLines(const Text &text, qreal width)
{
    QList<std::pair<int, qreal>> lines;
    const auto document = wordsDocument(text, width);
    auto *layout = document->documentLayout();
    for (auto block = document->begin(); block.isValid(); block = block.next()) {
        const qreal top = layout->blockBoundingRect(block).top();
        for (int i = 0; i < block.layout()->lineCount(); ++i) {
            const QTextLine line = block.layout()->lineAt(i);
            lines.append({block.position() + line.textStart(), top + line.y()});
        }
    }
    return lines;
}

QFont Traits::wordsFont(const QFont &font)
{
    QFont rounded = font;
    if (rounded.pointSizeF() != -1) {
        rounded.setPointSizeF(qRound(rounded.pointSizeF() * 2.0) / 2.0);
    }
    return rounded;
}

namespace
{
// how far down words of a height sit in a words rect of another, by the vertical alignment, as
// QQuickTextUtil::alignedY places the editing box's: what the painting and textCursorPoints both use
qreal topOffset(const Traits::Text &text, qreal rectHeight, qreal wordsHeight)
{
    const Qt::Alignment v = text.vertical();
    const qreal room = rectHeight - wordsHeight;
    return v == Qt::AlignBottom ? room : v == Qt::AlignVCenter ? room / 2 : 0;
}
}

qreal Traits::textWordsTopOffset(const Text &text, const QRectF &wordsRect)
{
    return topOffset(text, wordsRect.height(), wordsDocument(text, wordsRect.width())->size().height());
}

void Traits::keepTextAnchor(OptTuple &traits, const QRectF &wordsBefore)
{
    auto &geometry = std::get<Geometry::Opt>(traits);
    auto &text = std::get<Text::Opt>(traits);
    if (!geometry || !text || text->index() != Text::String || text->isBox()) {
        return;
    }
    const Qt::Alignment h = text->horizontal();
    const qreal factor = h == Qt::AlignRight ? 1.0 : h == Qt::AlignHCenter ? 0.5 : 0.0;
    const QRectF after = geometry->path.boundingRect();
    const qreal before = wordsBefore.left() + wordsBefore.width() * factor;
    const qreal dx = before - (after.left() + after.width() * factor);
    if (std::abs(dx) < 1e-6) {
        return;
    }
    geometry->path.translate(dx, 0);
    reInitTraits(traits);
}

QList<QPointF> Traits::textCursorPoints(const Text &text, qreal width, qreal height)
{
    QList<QPointF> points;
    const qreal top = height > 0 ? textWordsTopOffset(text, QRectF(0, 0, width, height)) : 0;
    const auto document = wordsDocument(text, width);
    auto *layout = document->documentLayout();
    const int length = text.text().size();
    for (int i = 0; i <= length; ++i) {
        const QTextBlock block = document->findBlock(i);
        if (!block.isValid()) {
            break;
        }
        const int at = i - block.position();
        const QTextLine line = block.layout()->lineForTextPosition(at);
        const QPointF blockTopLeft = layout->blockBoundingRect(block).topLeft();
        points << (line.isValid() ? blockTopLeft + QPointF(line.cursorToX(at), top + line.y()) : blockTopLeft + QPointF(0, top));
    }
    return points;
}

void Traits::drawTextWords(QPainter &painter, const Text &text, const QRectF &wordsRect)
{
    if (text.index() != Text::String) {
        painter.drawText(wordsRect, text.textFlags(), text.text());
        return;
    }
    // each block's lines where the document lays them, in the painter's pen (a block's layout holds
    // its place in the document: QTextLayout::draw adds it)
    const auto document = wordsDocument(text, wordsRect.width());
    // a box's words lower in it by their vertical alignment, as the editing box offsets them (asking
    // for the size also lays every block out: a long document's later ones are laid out lazily)
    const qreal wordsHeight = document->size().height();
    const QPointF topLeft = wordsRect.topLeft() + QPointF(0, topOffset(text, wordsRect.height(), wordsHeight));
    for (auto block = document->begin(); block.isValid(); block = block.next()) {
        block.layout()->draw(&painter, topLeft);
    }
}

QPointF Traits::textBoxWordsTopLeft(const QFont &font, const QRectF &outside)
{
    const auto pad = textLabelPadding(font);
    return outside.topLeft() + QPointF(pad.width(), pad.height());
}

QRectF Traits::textOutsideRect(const QFont &font, const QRectF &wordsRect)
{
    const auto pad = textLabelPadding(font);
    return wordsRect.adjusted(-pad.width(), -pad.height(), pad.width(), pad.height());
}

QColor Traits::labelRingColor(const QColor &fill)
{
    // one shade darker, as opaque as the fill
    QColor ring = fill.toHsl().darker(140);
    ring.setAlpha(fill.alpha());
    return ring;
}

QColor Traits::labelTextColor(const QColor &fill)
{
    // relative luminance (sRGB); above 0.3 the words go near-black, below it white
    auto linear = [](qreal c) {
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    const qreal luminance = 0.2126 * linear(fill.redF()) + 0.7152 * linear(fill.greenF()) + 0.0722 * linear(fill.blueF());
    return luminance > 0.3 ? QColor(10, 10, 10) : QColor(255, 255, 255);
}

QRectF Traits::visualRect(const OptTuple &traits)
{
    auto &visual = std::get<Visual::Opt>(traits);
    return visual ? visual->rect : QRectF{};
}

// QDebug operator<< declarations

// Traits

QDebug operator<<(QDebug debug, const Traits::Geometry &trait)
{
    using namespace Traits;
    QDebugStateSaver stateSaver(debug);
    debug.nospace();
    debug << "Geometry" << '(';
    debug << (const void *)&trait;
    debug << ",\n    path=" << trait.path;
    debug << ')';
    return debug;
}

QDebug operator<<(QDebug debug, const Traits::Interactive &trait)
{
    using namespace Traits;
    QDebugStateSaver stateSaver(debug);
    debug.nospace();
    debug << "Interactive" << '(';
    debug << (const void *)&trait;
    debug << ",\n    path=" << trait.path;
    debug << ')';
    return debug;
}

QDebug operator<<(QDebug debug, const Traits::Visual &trait)
{
    using namespace Traits;
    QDebugStateSaver stateSaver(debug);
    debug.nospace();
    debug << "Visual" << '(';
    debug << (const void *)&trait;
    debug << ",\n    rect=" << trait.rect;
    debug << ')';
    return debug;
}

QDebug operator<<(QDebug debug, const Traits::Stroke &trait)
{
    using namespace Traits;
    QDebugStateSaver stateSaver(debug);
    debug.nospace();
    debug << "Stroke" << '(';
    debug << (const void *)&trait;
    debug << ",\n    pen=" << trait.pen;
    debug << ",\n    path=" << trait.path;
    debug << ')';
    return debug;
}

QDebug operator<<(QDebug debug, const Traits::Fill &trait)
{
    using namespace Traits;
    QDebugStateSaver stateSaver(debug);
    debug.nospace();
    debug << "Fill" << '(';
    debug << (const void *)&trait;
    debug << ", ";
    switch (trait.index()) {
    case Fill::Brush:
        debug << std::get<Fill::Brush>(trait);
        break;
    case Fill::Blur:
        debug << std::get<Fill::Blur>(trait);
        break;
    case Fill::Pixelate:
        debug << std::get<Fill::Pixelate>(trait);
        break;
    default:
        break;
    }
    debug << ')';
    return debug;
}

QDebug operator<<(QDebug debug, const Traits::Highlight &trait)
{
    using namespace Traits;
    QDebugStateSaver stateSaver(debug);
    debug.nospace();
    debug << "Highlight" << '(';
    debug << (const void *)&trait;
    debug << ')';
    return debug;
}

QDebug operator<<(QDebug debug, const Traits::Arrow &trait)
{
    using namespace Traits;
    QDebugStateSaver stateSaver(debug);
    debug.nospace();
    debug << "Arrow" << '(';
    debug << (const void *)&trait;
    debug << ')';
    return debug;
}

QDebug operator<<(QDebug debug, const Traits::Text &trait)
{
    using namespace Traits;
    QDebugStateSaver stateSaver(debug);
    debug.nospace();
    debug << "Text" << '(';
    debug << (const void *)&trait;
    debug << ",\n    text=" << trait.text();
    debug << ",\n    brush=" << trait.brush;
    debug << ",\n    font=" << trait.font;
    debug << ')';
    return debug;
}

QDebug operator<<(QDebug debug, const Traits::Shadow &trait)
{
    using namespace Traits;
    QDebugStateSaver stateSaver(debug);
    debug.nospace();
    debug << "Shadow" << '(';
    debug << (const void *)&trait;
    debug << ",\n    enabled=" << trait.enabled;
    debug << ')';
    return debug;
}

QDebug operator<<(QDebug debug, const Traits::Meta::Delete &trait)
{
    using namespace Traits;
    QDebugStateSaver stateSaver(debug);
    debug.nospace();
    debug << "Delete" << '(';
    debug << (const void *)&trait;
    debug << ')';
    return debug;
}

QDebug operator<<(QDebug debug, const Traits::Meta::Crop &trait)
{
    using namespace Traits;
    QDebugStateSaver stateSaver(debug);
    debug.nospace();
    debug << "Crop" << '(';
    debug << (const void *)&trait;
    debug << ')';
    return debug;
}

QDebug operator<<(QDebug debug, const Traits::Meta::Transform &trait)
{
    using namespace Traits;
    QDebugStateSaver stateSaver(debug);
    debug.nospace();
    debug << "Transform" << '(';
    debug << (const void *)&trait;
    debug << ", " << trait.matrix;
    debug << ')';
    return debug;
}

QDebug operator<<(QDebug debug, const Traits::Meta::ColorAdjustment &trait)
{
    using namespace Traits::Meta;
    QDebugStateSaver stateSaver(debug);
    debug.nospace();
    debug << "ColorAdjustment" << '(';
    debug << (const void *)&trait;
    debug << ",\n    matrix=" << QDebug::toString(trait.matrix).remove(u'\n');
    debug << ",\n    gamma=" << trait.gamma;
    debug << ')';
    return debug;
}

// ImageEffects

QDebug operator<<(QDebug debug, const Traits::ImageEffects::Blur &ref)
{
    using namespace Traits::ImageEffects;
    QDebugStateSaver stateSaver(debug);
    debug.nospace();
    debug << "Blur" << '(';
    debug << (const void *)&ref;
    debug << ", strength=" << ref.strength();
    debug << ')';
    return debug;
}

QDebug operator<<(QDebug debug, const Traits::ImageEffects::Pixelate &ref)
{
    using namespace Traits::ImageEffects;
    QDebugStateSaver stateSaver(debug);
    debug.nospace();
    debug << "Pixelate" << '(';
    debug << (const void *)&ref;
    debug << ", strength=" << ref.strength();
    debug << ')';
    return debug;
}

// Optionals
// clang-format off
#define OPTIONAL_DEBUG_DEF(ClassName)\
QDebug operator<<(QDebug debug, const Traits::ClassName::Opt &optional)\
{\
    using namespace Traits;\
    QDebugStateSaver stateSaver(debug);\
    debug.nospace();\
    debug << "Opt" << '<';\
    if (optional.has_value()) {\
        debug << optional.value();\
    } else {\
        debug << #ClassName << "(0x0)";\
    }\
    debug << ">(" << &optional << ')';\
    return debug;\
}
// clang-format on
OPTIONAL_DEBUG_DEF(Geometry)
OPTIONAL_DEBUG_DEF(Interactive)
OPTIONAL_DEBUG_DEF(Visual)
OPTIONAL_DEBUG_DEF(Stroke)
OPTIONAL_DEBUG_DEF(Fill)
OPTIONAL_DEBUG_DEF(Highlight)
OPTIONAL_DEBUG_DEF(Arrow)
OPTIONAL_DEBUG_DEF(Text)
OPTIONAL_DEBUG_DEF(Shadow)
OPTIONAL_DEBUG_DEF(Meta::Delete)
OPTIONAL_DEBUG_DEF(Meta::Crop)
OPTIONAL_DEBUG_DEF(Meta::Transform)
OPTIONAL_DEBUG_DEF(Meta::ColorAdjustment)
#undef OPTIONAL_DEBUG_DEF

QDebug operator<<(QDebug debug, const Traits::OptTuple &optTuple)
{
    using namespace Traits;
    QDebugStateSaver stateSaver(debug);
    debug.nospace();
    debug << "OptTuple" << '(';
    debug << (const void *)&optTuple;
    debug << ",\n  " << std::get<Traits::Geometry::Opt>(optTuple);
    debug << ",\n  " << std::get<Traits::Interactive::Opt>(optTuple);
    debug << ",\n  " << std::get<Traits::Visual::Opt>(optTuple);
    debug << ",\n  " << std::get<Traits::Stroke::Opt>(optTuple);
    debug << ",\n  " << std::get<Traits::Fill::Opt>(optTuple);
    debug << ",\n  " << std::get<Traits::Highlight::Opt>(optTuple);
    debug << ",\n  " << std::get<Traits::Arrow::Opt>(optTuple);
    debug << ",\n  " << std::get<Traits::Text::Opt>(optTuple);
    debug << ",\n  " << std::get<Traits::Shadow::Opt>(optTuple);
    debug << ",\n  " << std::get<Traits::Meta::Delete::Opt>(optTuple);
    debug << ",\n  " << std::get<Traits::Meta::Crop::Opt>(optTuple);
    debug << ",\n  " << std::get<Traits::Meta::Transform::Opt>(optTuple);
    debug << ",\n  " << std::get<Traits::Meta::ColorAdjustment::Opt>(optTuple);
    debug << ')';
    return debug;
}

#include "moc_traits.cpp"
