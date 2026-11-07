/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#include "StartupClock.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QList>
#include <QTimer>

#include <utility>

#include <cstdio>
#include <ctime>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace
{
struct Clock {
    QElapsedTimer sinceMain;
    double mainAt = 0; // ms from the process's start to main()
    QList<std::pair<QString, double>> steps;
    QString lastStep = u"main"_s;
    QTimer *watch = nullptr;
    double lastTick = 0;
    double longestGap = 0;
    QString longestGapStep;
    double longestGapAt = 0;
    bool done = false;
    QString line;
};

Clock &state()
{
    static Clock c;
    return c;
}

// The process's start, from the kernel: /proc/self/stat field 22, in clock ticks since boot.
double msSinceProcessStart()
{
    QFile stat(u"/proc/self/stat"_s);
    if (!stat.open(QIODevice::ReadOnly)) {
        return 0;
    }
    const QByteArray text = stat.readAll();
    const int close = text.lastIndexOf(')'); // the name can hold spaces
    const QList<QByteArray> fields = text.mid(close + 2).split(' ');
    // fields[0] is field 3 (state), so field 22 is fields[19]
    if (fields.size() < 20) {
        return 0;
    }
    const double startTicks = fields[19].toDouble();
    timespec now{};
    clock_gettime(CLOCK_BOOTTIME, &now);
    const double nowMs = now.tv_sec * 1000.0 + now.tv_nsec / 1e6;
    return nowMs - startTicks * 1000.0 / double(sysconf(_SC_CLK_TCK));
}

double now()
{
    return state().mainAt + state().sinceMain.nsecsElapsed() / 1e6;
}

void writeLine(const QString &why)
{
    auto &c = state();
    if (c.done) {
        return;
    }
    c.done = true;
    if (c.watch) {
        c.watch->stop();
    }
    QString line = u"ECTOLENS-STARTUP"_s;
    // the process's start on the wall clock, to line up with other logs
    line += u" started=%1"_s.arg(QDateTime::currentMSecsSinceEpoch() - qint64(now()));
    line += u" main=%1"_s.arg(qRound(c.mainAt));
    for (const auto &[step, at] : std::as_const(c.steps)) {
        line += u" %1=%2"_s.arg(step).arg(qRound(at));
    }
    line += u" longest-stall=%1ms in=%2 at=%3 end=%4"_s.arg(qRound(c.longestGap)).arg(c.longestGapStep).arg(qRound(c.longestGapAt)).arg(why);
    c.line = line;
    const double drawn = [&] {
        for (const auto &[step, at] : std::as_const(c.steps)) {
            if (step == u"drawn") {
                return at;
            }
        }
        return -1.0;
    }();
    // one line in the system log per capture; a warning when the overlay took over a second to
    // answer, so a slow start is found without anyone timing it
    const double press = [&] {
        for (const auto &[step, at] : std::as_const(c.steps)) {
            if (step == u"press"_s) {
                return at;
            }
        }
        return c.mainAt;
    }();
    const bool slow = c.longestGap > 500 || (drawn >= 0 && drawn - press > 1000);
    std::fprintf(stderr, "%s%s\n", slow ? "ECTOLENS-STARTUP-SLOW " : "", qPrintable(line));
    std::fflush(stderr);
}
}

void StartupClock::start()
{
    auto &c = state();
    c.mainAt = msSinceProcessStart();
    c.sinceMain.start();
    c.lastTick = now();
    c.watch = new QTimer(QCoreApplication::instance());
    c.watch->setInterval(10);
    c.watch->setTimerType(Qt::PreciseTimer);
    QObject::connect(c.watch, &QTimer::timeout, [] {
        auto &c = state();
        const double t = now();
        const double gap = t - c.lastTick;
        if (gap > c.longestGap) {
            c.longestGap = gap;
            c.longestGapStep = c.lastStep;
            c.longestGapAt = c.lastTick;
        }
        c.lastTick = t;
    });
    c.watch->start();
    // nothing drew within 20 s: write what there is
    QTimer::singleShot(20000, QCoreApplication::instance(), [] {
        writeLine(u"timeout"_s);
    });
}

void StartupClock::mark(const QString &step)
{
    auto &c = state();
    if (c.done) {
        return;
    }
    for (const auto &[name, at] : std::as_const(c.steps)) {
        if (name == step) {
            return;
        }
    }
    c.steps.append({step, now()});
    c.lastStep = step;
}

void StartupClock::overlayDrawn()
{
    if (state().done) {
        return;
    }
    mark(u"drawn"_s);
    // a stall just after the overlay shows freezes it as much as one before
    QTimer::singleShot(1500, QCoreApplication::instance(), [] {
        writeLine(u"drawn"_s);
    });
}

void StartupClock::finish(const QString &why)
{
    writeLine(why);
}

QString StartupClock::summary()
{
    return state().line;
}
