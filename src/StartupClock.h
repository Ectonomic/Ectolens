/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#pragma once

#include <QString>

/**
 * Times a capture from the key press to the overlay being drawn and answering
 * (Print Screen must not freeze the whole desktop while it loads).
 *
 * Each step is marked with the milliseconds since the process started (the kernel's own
 * start time, so loading the program counts too). A watch on the event loop records the
 * longest time it couldn't answer input, and which step it was in. One line goes to the
 * journal per capture: "ECTOLENS-STARTUP ...", and "ECTOLENS-STARTUP-SLOW ..." as a warning
 * when the overlay took longer than a second to answer.
 */
namespace StartupClock
{
// Starts the clock and the event-loop watch. Called once, early in main().
void start();
// A step reached. A step already marked keeps its first time.
void mark(const QString &step);
// The overlay is drawn on every screen: the watch keeps going a moment longer
// (stalls just after it shows still freeze it), then the line is written.
void overlayDrawn();
// Writes the line now, if it hasn't been (a capture without the overlay).
void finish(const QString &why);
// The line, for the off-screen tests.
QString summary();
}
