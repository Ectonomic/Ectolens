/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#pragma once

#include <QString>
#include <QtGlobal>

#include <cstdio>

// Ectolens test hook: drives the capture overlay from ECTOLENS_TEST_SCRIPT, a
// list of steps separated by ';', for the off-screen overlay test
// (tests/e2e/overlay). Does nothing unless the variable is set.
//   frame:X,Y,W,H   frame that area
//   hover:X,Y       point there, as the pointer would
//   click           take what is pointed at (prints "ECTOLENS-TOOK window|screen X Y W H" or "nothing")
//   mark:NAME       print "ECTOLENS-STEP NAME" and wait for ECTOLENS_TEST_ACK/NAME
//   locate:NAME     print where the control with that objectName is: "ECTOLENS-AT NAME X Y"
//   record | pause | resume | stop | wait:MS
//   wlocate:NAME    where a control is in the Ectolens window: "ECTOLENS-WAT NAME X Y" (window coordinates)
//   wclick:NAME     press a button in the Ectolens window from inside the app
//   menus           how many menus are open: "ECTOLENS-MENUS N"
//   tool:TYPE       pick a marking tool (RectangleTool, CropTool, NoTool…); draw:X1,Y1,X2,Y2 marks with it
//   size:W,H        the frame label's size menu picks W×H; framerect prints "ECTOLENS-FRAME X Y W H"
//   canvas          the picture's size in pixels: "ECTOLENS-CANVAS W H"
//   save | message:error|scanned | wresize:W,H
// Prints "ECTOLENS-SAVED PATH" when a recording is saved, and report() lines while a script runs.
namespace TestScript
{
void startIfRequested();
/// Ectolens test hook: print a line for the test to judge ("ECTOLENS-CLIPBOARD text #A1B2C3"),
/// only while a test script runs. Inline, so code shared with the unit tests needs nothing more.
inline void report(const QString &line)
{
    static const bool active = !qEnvironmentVariableIsEmpty("ECTOLENS_TEST_SCRIPT");
    if (active) {
        std::printf("%s\n", qPrintable(line));
        std::fflush(stdout);
    }
}
}
