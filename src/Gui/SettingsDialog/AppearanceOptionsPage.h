/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#pragma once

#include <QWidget>

class QButtonGroup;
class QSlider;
class QSpinBox;
class KColorButton;
class AppearancePreview;

/**
 * Ectolens: Settings › Appearance (design proposal 2): the look (Ecto, or your KDE colors
 * and fonts), the accent (Ecto green, your KDE accent, or a color you pick) and how dark
 * the rest of the screen gets around the frame, with a preview.
 *
 * Its widgets are named kcfg_<entry> so the settings dialog loads and saves them; the
 * radio buttons write into hidden spin boxes that carry the enum values.
 */
class AppearanceOptionsPage : public QWidget
{
    Q_OBJECT
public:
    explicit AppearanceOptionsPage(QWidget *parent = nullptr);

private:
    void syncRadios();
    QSpinBox *m_look;
    QSpinBox *m_accent;
    QButtonGroup *m_lookGroup;
    QButtonGroup *m_accentGroup;
    KColorButton *m_custom;
    QSlider *m_dim;
    AppearancePreview *m_preview;
};
