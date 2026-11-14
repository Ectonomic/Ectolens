/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#include "PrivateInfoSettings.h"

#include "PrivateInfo.h"
#include "settings.h"

void PrivateInfoSettings::follow()
{
    static bool following = false;
    PrivateInfo::setHideList(Settings::alwaysHide());
    if (!following) {
        following = true;
        QObject::connect(Settings::self(), &Settings::alwaysHideChanged, Settings::self(), [] {
            PrivateInfo::setHideList(Settings::alwaysHide());
        });
    }
}
