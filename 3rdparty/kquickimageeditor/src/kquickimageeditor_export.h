/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Ectolens builds its copy of KQuickImageEditor into the ectolens binary (a static library),
 * so nothing is exported; upstream generates this file with ecm_generate_export_header.
 */
#pragma once

#define KQUICKIMAGEEDITOR_EXPORT
#define KQUICKIMAGEEDITOR_NO_EXPORT
