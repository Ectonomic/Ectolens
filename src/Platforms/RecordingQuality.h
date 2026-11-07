/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#pragma once

#include <PipeWireBaseEncodedStream>

#include <functional>

// Ectolens: what each recording quality choice asks of the encoder.
// The values were measured on a 3440x1440 test clip; see design/mock section 01.
namespace RecordingQuality
{
enum Preset {
    Small, // x264 quality 23: small files for chat and bug reports
    High, // x264 quality 16: sharp text, plays everywhere (the default)
    Best, // x264 quality 12 with full color detail (4:4:4)
    Custom, // the person's own values
};

struct CustomValues {
    qreal qualityLevel = 16;
    QString speedPreset = QStringLiteral("veryfast");
    qreal keyframeSeconds = 2;
    int bFrames = -1; // -1: the speed preset decides
    bool fullColor = false;
    QString extraOptions;
};

// The encoder (an FFmpeg name: libx264, libx265, libsvtav1, h264_nvenc, hevc_nvenc,
// av1_nvenc) decides what the numbers mean, so each family has its own values.
// x264 and SVT-AV1 High were measured (design/mock section 01); the others follow
// each encoder's documented equivalents and are listed in docs/deferred.md.
PipeWireBaseEncodedStream::EncoderSettings settingsFor(Preset preset, const CustomValues &custom = {}, const QString &encoderName = QStringLiteral("libx264"));

// The encoder a recording uses: the chosen one when this computer's FFmpeg has it,
// otherwise none by name (empty), and KPipeWire picks an H.264 encoder the way
// Spectacle does: the graphics chip through VA-API, x264, then OpenH264. Fedora's and
// openSUSE's own FFmpeg have no x264, Ectolens's default. A graphics card's encoder
// stays as chosen: Settings lists one only where it opened, and KPipeWire falls back
// the same way when it doesn't. `exists` is PipeWireBaseEncodedStream::hasEncoder.
QString encoderToUse(const QString &chosen, const std::function<bool(const QString &)> &exists = &PipeWireBaseEncodedStream::hasEncoder);
}
