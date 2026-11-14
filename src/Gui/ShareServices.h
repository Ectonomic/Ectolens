/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#pragma once

#include <QString>
#include <QUrl>
#include <QVariantList>

/**
 * Which of KDE's share services (Purpose's "Export" plugins) Ectolens offers.
 *
 * Some are never offered: they repeat an After Capture row, upload in public without asking,
 * or sign in to a service that no longer lets KDE in. Others are offered only while they can
 * work on this computer: Send via Email needs an email app, Send to Device a paired device.
 * Every Share list (After Capture, Settings › General, the overlay's and the window's) comes
 * from offered(), so a service is left out everywhere or nowhere.
 */
namespace ShareServices
{
/// The services Ectolens offers for a file like `sample`: [{id, name, icon}]
QVariantList offered(const QUrl &sample);

/// Every service KDE has for a file like `sample`, offered or not:
/// [{id, name, icon, why}], `why` empty when it is offered, else why it is left out
QVariantList all(const QUrl &sample);

/// Ectolens never offers this service (no translation needed: safe before the app starts)
bool isRetired(const QString &pluginId);

/// Why Ectolens never offers this service ("" for one it can offer)
QString retiredBecause(const QString &pluginId);

/// An app is set to open mailto: links (what Send via Email starts)
bool hasMailApp();

/// KDE Connect knows a paired device (asked without starting KDE Connect)
bool hasPairedDevice();
}
