/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#pragma once

#include <QHash>
#include <QRect>
#include <QStringList>
#include <QtWaylandClient/QWaylandClientExtension>

#include "qwayland-plasma-window-management.h"

#include <memory>

/**
 * Ectolens: knows where every window is, from KWin's window-management
 * protocol (Ectolens's desktop file grants it), so the capture overlay can take
 * a window by pointing at it. Positions are in KWin's logical coordinates, the
 * same the selection uses.
 */
class WindowPicker : public QWaylandClientExtensionTemplate<WindowPicker>, public QtWayland::org_kde_plasma_window_management
{
    Q_OBJECT

public:
    struct Window {
        QString uuid;
        QRect geometry;
        QString iconName; // the theme icon the window shows in the task manager
        QString appId; // its desktop file name, like org.kde.konsole
        QString title;
        bool maximized = false;
        bool active = false;
    };

    static WindowPicker *instance();

    /// The topmost window under a point, among the windows on the current virtual
    /// desktop and activity, leaving out minimized windows, windows that skip the
    /// taskbar (panels, the desktop) and Ectolens's own windows.
    std::optional<Window> windowAt(const QPointF &position) const;
    /// The windows stacked above one, bottom to top, that can be seen on this desktop.
    QList<Window> windowsAbove(const QString &uuid) const;
    /// Ectolens: every window one could point at now (the current desktop and activity, not
    /// minimized, not a panel, not Ectolens's own), topmost first: what an agent may ask for.
    QList<Window> visibleWindows() const;
    /// A window by its id, wherever it is.
    std::optional<Window> window(const QString &uuid) const;
    /// The app's name from its desktop file ("Konsole"), or the window title's last part.
    static QString appName(const Window &window);

private Q_SLOTS:
    void setCurrentDesktop(const QString &id);
    void setCurrentActivity(const QString &id);

protected:
    void org_kde_plasma_window_management_window_with_uuid(uint32_t id, const QString &uuid) override;
    void org_kde_plasma_window_management_stacking_order_uuid_changed(const QString &uuids) override;

private:
    WindowPicker();
    class PlasmaWindow;
    QHash<QString, std::shared_ptr<PlasmaWindow>> m_windows;
    QStringList m_stacking; // bottom to top
    QString m_currentDesktop; // from KWin's VirtualDesktopManager
    QString m_currentActivity; // from the activity manager
};
