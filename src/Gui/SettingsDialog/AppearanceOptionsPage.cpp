/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#include "AppearanceOptionsPage.h"

#include "settings.h"

#include <KColorButton>
#include <KLocalizedString>

#include <QApplication>
#include <QButtonGroup>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

/// A small drawing of the overlay: a dimmed screen, a framed window, the accent and a row.
class AppearancePreview : public QWidget
{
public:
    explicit AppearancePreview(QWidget *parent)
        : QWidget(parent)
    {
        setMinimumSize(420, 132);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    QColor accent = QColor(u"#c8f22e"_s);
    qreal dim = 0.4;
    bool ecto = true;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF all = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath clip;
        clip.addRoundedRect(all, 6, 6);
        p.setClipPath(clip);
        // a desktop with two windows
        QLinearGradient sky(all.topLeft(), all.bottomRight());
        sky.setColorAt(0, QColor(u"#24324a"_s));
        sky.setColorAt(1, QColor(u"#3a2a48"_s));
        p.fillRect(all, sky);
        const QRectF other(all.width() * 0.62, 20, all.width() * 0.3, 80);
        p.fillRect(other, QColor(u"#2b2f36"_s));
        const QRectF frame(all.width() * 0.16, 30, all.width() * 0.4, 74);
        p.fillRect(frame, QColor(u"#1d1f22"_s));
        p.fillRect(QRectF(frame.left(), frame.top(), frame.width(), 12), QColor(u"#2a2d31"_s));
        for (int i = 0; i < 4; ++i) {
            p.fillRect(QRectF(frame.left() + 10, frame.top() + 22 + i * 11, frame.width() * (0.7 - i * 0.12), 4), QColor(u"#6b7280"_s));
        }
        // dim the rest
        QPainterPath rest;
        rest.addRect(all);
        QPainterPath hole;
        hole.addRect(frame);
        p.fillPath(rest.subtracted(hole), QColor(0, 0, 0, int(dim * 255)));
        // the frame and its handles in the accent
        p.setPen(QPen(accent, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(frame.adjusted(0.5, 0.5, -0.5, -0.5));
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        for (const QPointF c : {frame.topLeft(), frame.topRight(), frame.bottomLeft(), frame.bottomRight(), QPointF(frame.center().x(), frame.top()),
                                QPointF(frame.center().x(), frame.bottom()), QPointF(frame.left(), frame.center().y()), QPointF(frame.right(), frame.center().y())}) {
            p.drawEllipse(c, 4, 4);
        }
        // a row under the frame
        const QColor rowBg = ecto ? QColor(u"#111113"_s) : palette().window().color();
        const QColor rowBorder = ecto ? QColor(u"#27272a"_s) : palette().mid().color();
        const QRectF row(frame.center().x() - 70, frame.bottom() + 8, 140, 20);
        p.setPen(QPen(rowBorder, 1));
        p.setBrush(rowBg);
        p.drawRoundedRect(row, 10, 10);
        p.setPen(ecto ? QColor(u"#fafafa"_s) : palette().windowText().color());
        QFont f = font();
        if (ecto) {
            f.setFamily(u"Geist"_s);
        }
        f.setPixelSize(10);
        f.setWeight(QFont::Medium);
        p.setFont(f);
        p.drawText(row, Qt::AlignCenter, i18nc("@info preview of the overlay's buttons", "Capture   Record   Extract Text"));
        // the accent line under the tool in use, in a row above
        const QRectF tools(frame.center().x() - 50, frame.top() - 26, 100, 20);
        p.setPen(QPen(rowBorder, 1));
        p.setBrush(rowBg);
        p.drawRoundedRect(tools, 10, 10);
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        p.drawRoundedRect(QRectF(tools.left() + 14, tools.bottom() - 5, 10, 2), 1, 1);
    }
};

AppearanceOptionsPage::AppearanceOptionsPage(QWidget *parent)
    : QWidget(parent)
    , m_look(new QSpinBox(this))
    , m_accent(new QSpinBox(this))
    , m_lookGroup(new QButtonGroup(this))
    , m_accentGroup(new QButtonGroup(this))
    , m_custom(new KColorButton(this))
    , m_dim(new QSlider(Qt::Horizontal, this))
    , m_preview(new AppearancePreview(this))
{
    auto form = new QFormLayout(this);
    form->setFormAlignment(Qt::AlignHCenter | Qt::AlignTop);

    m_look->setObjectName(u"kcfg_look"_s);
    m_look->hide();
    m_accent->setObjectName(u"kcfg_accent"_s);
    m_accent->hide();

    // Look
    auto lookBox = new QVBoxLayout;
    auto ecto = new QRadioButton(i18nc("@option:radio look", "Ecto: dark rows, Geist type"), this);
    auto kde = new QRadioButton(i18nc("@option:radio look", "Your KDE colors and fonts"), this);
    m_lookGroup->addButton(ecto, Settings::EctoLook);
    m_lookGroup->addButton(kde, Settings::KdeLook);
    lookBox->addWidget(ecto);
    lookBox->addWidget(kde);
    form->addRow(i18nc("@label", "Look:"), lookBox);

    // Accent
    auto accentBox = new QVBoxLayout;
    auto chip = [this](const QColor &color) {
        QPixmap pix(18, 18);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QColor(255, 255, 255, 50));
        p.setBrush(color);
        p.drawRoundedRect(QRectF(0.5, 0.5, 17, 17), 4, 4);
        return QIcon(pix);
    };
    auto green = new QRadioButton(i18nc("@option:radio accent", "Ecto green"), this);
    green->setIcon(chip(QColor(u"#c8f22e"_s)));
    auto kdeAccent = new QRadioButton(i18nc("@option:radio accent", "Your KDE accent"), this);
    kdeAccent->setIcon(chip(qApp->palette().highlight().color()));
    auto customRow = new QHBoxLayout;
    auto custom = new QRadioButton(i18nc("@option:radio accent", "Custom:"), this);
    m_custom->setObjectName(u"kcfg_customAccentColor"_s);
    customRow->addWidget(custom);
    customRow->addWidget(m_custom);
    customRow->addStretch();
    m_accentGroup->addButton(green, Settings::EctoGreenAccent);
    m_accentGroup->addButton(kdeAccent, Settings::KdeAccent);
    m_accentGroup->addButton(custom, Settings::CustomAccent);
    accentBox->addWidget(green);
    accentBox->addWidget(kdeAccent);
    accentBox->addLayout(customRow);
    auto hint = new QLabel(i18nc("@info", "Colors the frame, the window under the pointer, the tool in use and checked boxes."), this);
    hint->setWordWrap(true);
    hint->setEnabled(false);
    accentBox->addWidget(hint);
    form->addRow(i18nc("@label", "Accent:"), accentBox);

    // Dim the rest
    auto dimBox = new QVBoxLayout;
    m_dim->setObjectName(u"kcfg_dimAmount"_s);
    m_dim->setRange(0, 80);
    m_dim->setPageStep(10);
    m_dim->setMinimumWidth(260);
    dimBox->addWidget(m_dim);
    auto ends = new QHBoxLayout;
    auto none = new QLabel(i18nc("@item:inrange no dimming", "None"), this);
    auto now = new QLabel(this);
    auto darkest = new QLabel(i18nc("@item:inrange the most dimming", "Dark"), this);
    for (auto l : {none, now, darkest}) {
        l->setEnabled(false);
    }
    ends->addWidget(none);
    ends->addStretch();
    ends->addWidget(now);
    ends->addStretch();
    ends->addWidget(darkest);
    dimBox->addLayout(ends);
    form->addRow(i18nc("@label", "Dim the rest:"), dimBox);

    form->addRow(i18nc("@label", "Preview:"), m_preview);

    auto refresh = [this, now, custom] {
        m_custom->setEnabled(custom->isChecked());
        now->setText(i18nc("@item:inrange %1 percent", "%1%", m_dim->value()));
        const int accent = m_accentGroup->checkedId();
        m_preview->accent = accent == Settings::KdeAccent ? qApp->palette().highlight().color()
            : accent == Settings::CustomAccent              ? m_custom->color()
                                                            : QColor(u"#c8f22e"_s);
        m_preview->dim = m_dim->value() / 100.0;
        m_preview->ecto = m_lookGroup->checkedId() != Settings::KdeLook;
        m_preview->update();
    };
    connect(m_lookGroup, &QButtonGroup::idClicked, m_look, [this, refresh](int id) {
        m_look->setValue(id);
        refresh();
    });
    connect(m_accentGroup, &QButtonGroup::idClicked, m_accent, [this, refresh](int id) {
        m_accent->setValue(id);
        refresh();
    });
    // the dialog loads values into the hidden spin boxes; the radios follow
    connect(m_look, &QSpinBox::valueChanged, this, [this, refresh] {
        syncRadios();
        refresh();
    });
    connect(m_accent, &QSpinBox::valueChanged, this, [this, refresh] {
        syncRadios();
        refresh();
    });
    connect(m_custom, &KColorButton::changed, this, refresh);
    connect(m_dim, &QSlider::valueChanged, this, refresh);
    syncRadios();
    refresh();
}

void AppearanceOptionsPage::syncRadios()
{
    if (auto b = m_lookGroup->button(m_look->value())) {
        b->setChecked(true);
    }
    if (auto b = m_accentGroup->button(m_accent->value())) {
        b->setChecked(true);
    }
}

#include "moc_AppearanceOptionsPage.cpp"
