/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#pragma once

#include <QEventLoopLocker>
#include <QImage>
#include <QQuickView>

#include <memory>

/**
 * Ectolens: Pin (design proposal 2, section 05). A capture floating above your windows as a
 * reference while you work. Drag it anywhere; Esc closes it; Ctrl+C copies it.
 *
 * On Wayland it is a layer surface on the top layer, so it stays above normal windows,
 * and dragging moves it by changing its margins from the screen's top-left corner.
 */
class PinWindow : public QQuickView
{
    Q_OBJECT
    Q_PROPERTY(QUrl imageUrl READ imageUrl CONSTANT FINAL)
    Q_PROPERTY(QSizeF imageSize READ imageSize CONSTANT FINAL)

public:
    /// Pin a picture where it was on screen (logical, global).
    static PinWindow *pin(const QImage &image, const QRectF &where);
    ~PinWindow() override;

    /// How many pins are up.
    static int count();
    /// The app is done otherwise: quit when the last pin closes.
    static void quitAfterLast();

    QUrl imageUrl() const;
    QSizeF imageSize() const;

    Q_INVOKABLE void moveBy(qreal dx, qreal dy);
    Q_INVOKABLE void copy() const;
    Q_INVOKABLE void unpin();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    PinWindow(const QImage &image, const QRectF &where);
    void place();
    QImage m_image;
    QString m_file;
    QPointF m_topLeft; // logical, global
    QScreen *m_screenAt = nullptr;
    std::unique_ptr<QEventLoopLocker> m_keepRunning;
};
