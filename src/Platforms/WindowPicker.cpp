/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#include "WindowPicker.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>

#include <KService>

#include <unistd.h>

using namespace Qt::StringLiterals;

class WindowPicker::PlasmaWindow : public QtWayland::org_kde_plasma_window
{
public:
    explicit PlasmaWindow(::org_kde_plasma_window *object)
        : QtWayland::org_kde_plasma_window(object)
    {
    }
    ~PlasmaWindow() override
    {
        destroy();
    }

    QRect geometry;
    uint32_t state = 0;
    uint32_t pid = 0;
    bool unmapped = false;
    QStringList desktops; // virtual desktop ids; empty means all
    QStringList activities; // activity ids; empty means all
    QString iconName;
    QString appId;
    QString title;

protected:
    void org_kde_plasma_window_geometry(int32_t x, int32_t y, uint32_t width, uint32_t height) override
    {
        geometry = QRect(x, y, int(width), int(height));
    }
    void org_kde_plasma_window_state_changed(uint32_t flags) override
    {
        state = flags;
    }
    void org_kde_plasma_window_pid_changed(uint32_t value) override
    {
        pid = value;
    }
    void org_kde_plasma_window_themed_icon_name_changed(const QString &name) override
    {
        iconName = name;
    }
    void org_kde_plasma_window_app_id_changed(const QString &id) override
    {
        appId = id;
    }
    void org_kde_plasma_window_title_changed(const QString &value) override
    {
        title = value;
    }
    void org_kde_plasma_window_unmapped() override
    {
        unmapped = true;
    }
    void org_kde_plasma_window_virtual_desktop_entered(const QString &id) override
    {
        if (!desktops.contains(id)) {
            desktops << id;
        }
    }
    void org_kde_plasma_window_virtual_desktop_left(const QString &id) override
    {
        desktops.removeAll(id);
    }
    void org_kde_plasma_window_activity_entered(const QString &id) override
    {
        if (!activities.contains(id)) {
            activities << id;
        }
    }
    void org_kde_plasma_window_activity_left(const QString &id) override
    {
        activities.removeAll(id);
    }
};

WindowPicker::WindowPicker()
    : QWaylandClientExtensionTemplate<WindowPicker>(16) // 16 still sends the stacking order as uuids
{
    initialize();

    // Which virtual desktop and activity are current: only their windows can be taken.
    auto bus = QDBusConnection::sessionBus();
    QDBusInterface desktops(u"org.kde.KWin"_s, u"/VirtualDesktopManager"_s, u"org.kde.KWin.VirtualDesktopManager"_s, bus);
    m_currentDesktop = desktops.property("current").toString();
    bus.connect(u"org.kde.KWin"_s, u"/VirtualDesktopManager"_s, u"org.kde.KWin.VirtualDesktopManager"_s, u"currentChanged"_s, this, SLOT(setCurrentDesktop(QString)));
    QDBusInterface activities(u"org.kde.ActivityManager"_s, u"/ActivityManager/Activities"_s, u"org.kde.ActivityManager.Activities"_s, bus);
    const QDBusReply<QString> activity = activities.call(u"CurrentActivity"_s);
    if (activity.isValid()) {
        m_currentActivity = activity.value();
    }
    bus.connect(u"org.kde.ActivityManager"_s, u"/ActivityManager/Activities"_s, u"org.kde.ActivityManager.Activities"_s, u"CurrentActivityChanged"_s, this,
                SLOT(setCurrentActivity(QString)));
}

void WindowPicker::setCurrentDesktop(const QString &id)
{
    m_currentDesktop = id;
}

void WindowPicker::setCurrentActivity(const QString &id)
{
    m_currentActivity = id;
}

WindowPicker *WindowPicker::instance()
{
    static WindowPicker *picker = new WindowPicker;
    return picker;
}

void WindowPicker::org_kde_plasma_window_management_window_with_uuid(uint32_t id, const QString &uuid)
{
    Q_UNUSED(id)
    m_windows.insert(uuid, std::make_shared<PlasmaWindow>(get_window_by_uuid(uuid)));
}

void WindowPicker::org_kde_plasma_window_management_stacking_order_uuid_changed(const QString &uuids)
{
    m_stacking = uuids.split(u';', Qt::SkipEmptyParts);
}

QList<WindowPicker::Window> WindowPicker::visibleWindows() const
{
    constexpr uint32_t minimized = ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MINIMIZED;
    constexpr uint32_t skipTaskbar = ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_SKIPTASKBAR;
    const uint32_t self = uint32_t(getpid());
    QList<Window> list;
    for (auto it = m_stacking.crbegin(); it != m_stacking.crend(); ++it) {
        const auto window = m_windows.value(*it);
        if (!window || window->unmapped || window->pid == self || (window->state & (minimized | skipTaskbar))) {
            continue;
        }
        const bool onAllDesktops = window->state & ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_ON_ALL_DESKTOPS;
        if (!onAllDesktops && !window->desktops.isEmpty() && !m_currentDesktop.isEmpty() && !window->desktops.contains(m_currentDesktop)) {
            continue;
        }
        if (!window->activities.isEmpty() && !m_currentActivity.isEmpty() && !window->activities.contains(m_currentActivity)) {
            continue;
        }
        list << Window{*it, window->geometry, window->iconName, window->appId, window->title,
                       bool(window->state & ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MAXIMIZED),
                       bool(window->state & ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_ACTIVE)};
    }
    return list;
}

std::optional<WindowPicker::Window> WindowPicker::windowAt(const QPointF &position) const
{
    constexpr uint32_t minimized = ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MINIMIZED;
    constexpr uint32_t skipTaskbar = ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_SKIPTASKBAR;
    const uint32_t self = uint32_t(getpid());
    for (auto it = m_stacking.crbegin(); it != m_stacking.crend(); ++it) {
        const auto window = m_windows.value(*it);
        if (!window || window->unmapped || window->pid == self || (window->state & (minimized | skipTaskbar))) {
            continue;
        }
        // Only windows on the desktop and activity being shown (2026-11-01: windows on
        // other virtual desktops were outlined over empty space).
        const bool onAllDesktops = window->state & ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_ON_ALL_DESKTOPS;
        if (!onAllDesktops && !window->desktops.isEmpty() && !m_currentDesktop.isEmpty() && !window->desktops.contains(m_currentDesktop)) {
            continue;
        }
        if (!window->activities.isEmpty() && !m_currentActivity.isEmpty() && !window->activities.contains(m_currentActivity)) {
            continue;
        }
        if (window->geometry.contains(position.toPoint())) {
            return Window{*it, window->geometry, window->iconName, window->appId, window->title, bool(window->state & ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MAXIMIZED)};
        }
    }
    return std::nullopt;
}

std::optional<WindowPicker::Window> WindowPicker::window(const QString &uuid) const
{
    const auto window = m_windows.value(uuid);
    if (!window || window->unmapped) {
        return std::nullopt;
    }
    return Window{uuid, window->geometry, window->iconName, window->appId, window->title, bool(window->state & ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MAXIMIZED)};
}

QString WindowPicker::appName(const Window &window)
{
    if (!window.appId.isEmpty()) {
        if (const auto service = KService::serviceByDesktopName(window.appId)) {
            return service->name();
        }
    }
    // "file.txt — Kate" names its app last
    const auto parts = window.title.split(u" — "_s);
    return parts.size() > 1 ? parts.last().trimmed() : window.title;
}

QList<WindowPicker::Window> WindowPicker::windowsAbove(const QString &uuid) const
{
    QList<Window> above;
    const int index = m_stacking.indexOf(uuid);
    if (index < 0) {
        return above;
    }
    constexpr uint32_t minimized = ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MINIMIZED;
    const uint32_t self = uint32_t(getpid());
    for (int i = index + 1; i < m_stacking.size(); ++i) {
        const auto window = m_windows.value(m_stacking[i]);
        if (!window || window->unmapped || window->pid == self || (window->state & minimized)) {
            continue;
        }
        const bool onAllDesktops = window->state & ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_ON_ALL_DESKTOPS;
        if (!onAllDesktops && !window->desktops.isEmpty() && !m_currentDesktop.isEmpty() && !window->desktops.contains(m_currentDesktop)) {
            continue;
        }
        if (!window->activities.isEmpty() && !m_currentActivity.isEmpty() && !window->activities.contains(m_currentActivity)) {
            continue;
        }
        above << Window{m_stacking[i], window->geometry, window->iconName, window->appId, window->title,
                        bool(window->state & ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MAXIMIZED)};
    }
    return above;
}
