/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#include "RecordingQuality.h"

using namespace Qt::StringLiterals;

namespace
{
struct Ladder {
    qreal small, high, best;
    bool bestIsFullColor;
    QString speedPreset;
};

Ladder ladderFor(const QString &encoderName)
{
    if (encoderName == u"libx265") {
        // x265 quality 28 matches x264's 23 (FFmpeg's H.265 guide); ultrafast is the
        // only preset near real time at full screen on a 16-core processor.
        return {28, 21, 17, true, u"ultrafast"_s};
    }
    if (encoderName == u"libsvtav1") {
        // Preset 10 at quality 30 was measured: closer to the screen than x264 High.
        return {38, 30, 24, false, u"10"_s};
    }
    if (encoderName.endsWith(u"_nvenc")) {
        // OBS uses the same numbers for NVENC's target quality as for x264.
        return {23, 16, 12, encoderName != u"av1_nvenc", u"p5"_s};
    }
    return {23, 16, 12, true, u"veryfast"_s}; // libx264, measured
}
}

QString RecordingQuality::encoderToUse(const QString &chosen, const std::function<bool(const QString &)> &exists)
{
    if (chosen.endsWith(u"_nvenc") || (!chosen.isEmpty() && exists(chosen))) {
        return chosen;
    }
    return {};
}

PipeWireBaseEncodedStream::EncoderSettings RecordingQuality::settingsFor(Preset preset, const CustomValues &custom, const QString &encoderName)
{
    const Ladder ladder = ladderFor(encoderName);
    PipeWireBaseEncodedStream::EncoderSettings settings;
    settings.encoderName = encoderName;
    settings.speedPreset = ladder.speedPreset;
    settings.keyframeIntervalSeconds = 2;
    switch (preset) {
    case Small:
        settings.qualityLevel = ladder.small;
        break;
    case High:
        settings.qualityLevel = ladder.high;
        break;
    case Best:
        settings.qualityLevel = ladder.best;
        settings.fullColor = ladder.bestIsFullColor;
        break;
    case Custom:
        settings.qualityLevel = custom.qualityLevel;
        settings.speedPreset = custom.speedPreset;
        settings.keyframeIntervalSeconds = custom.keyframeSeconds;
        if (custom.bFrames >= 0) {
            settings.bFrames = custom.bFrames;
        }
        settings.fullColor = custom.fullColor;
        settings.extraOptions = custom.extraOptions;
        break;
    }
    return settings;
}
