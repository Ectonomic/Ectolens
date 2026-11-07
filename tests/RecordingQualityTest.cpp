/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#include "Platforms/RecordingQuality.h"

#include <QTest>

using namespace Qt::StringLiterals;

// Each quality choice asks for the values that were measured (design/mock section 01).
class RecordingQualityTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testPresets()
    {
        const auto small = RecordingQuality::settingsFor(RecordingQuality::Small);
        QCOMPARE(small.qualityLevel, 23.0);
        QCOMPARE(small.fullColor, false);
        const auto high = RecordingQuality::settingsFor(RecordingQuality::High);
        QCOMPARE(high.qualityLevel, 16.0);
        QCOMPARE(high.speedPreset, u"veryfast"_s);
        QCOMPARE(high.keyframeIntervalSeconds, 2.0);
        QCOMPARE(high.fullColor, false);
        const auto best = RecordingQuality::settingsFor(RecordingQuality::Best);
        QCOMPARE(best.qualityLevel, 12.0);
        QCOMPARE(best.fullColor, true);
        // No preset forces B-frames; the speed preset decides.
        QVERIFY(!high.bFrames.has_value());
    }

    // A recording uses the chosen encoder only where this computer's FFmpeg has it; without
    // it, none by name, so KPipeWire picks the system's own H.264, as Spectacle does.
    void testEncoderToUse()
    {
        const auto has = [](const QStringList &names) {
            return [names](const QString &name) {
                return names.contains(name);
            };
        };
        QCOMPARE(RecordingQuality::encoderToUse(u"libx264"_s, has({u"libx264"_s, u"libsvtav1"_s})), u"libx264"_s);
        // Fedora's and openSUSE's own FFmpeg: no x264, though SVT-AV1 is there; H.264 still, the system's
        QCOMPARE(RecordingQuality::encoderToUse(u"libx264"_s, has({u"libsvtav1"_s})), QString());
        QCOMPARE(RecordingQuality::encoderToUse(u"libx265"_s, has({})), QString());
        // a graphics card's encoder stays as chosen: Settings lists one only where it opened
        QCOMPARE(RecordingQuality::encoderToUse(u"h264_nvenc"_s, has({})), u"h264_nvenc"_s);
        QCOMPARE(RecordingQuality::encoderToUse(QString(), has({u"libx264"_s})), QString());
        // the real lookup, in this computer's FFmpeg
        QCOMPARE(RecordingQuality::encoderToUse(u"no_such_encoder"_s), QString());
    }

    void testEachEncoderHasItsOwnLadder()
    {
        QCOMPARE(RecordingQuality::settingsFor(RecordingQuality::High).encoderName, u"libx264"_s);
        const auto av1 = RecordingQuality::settingsFor(RecordingQuality::High, {}, u"libsvtav1"_s);
        QCOMPARE(av1.encoderName, u"libsvtav1"_s);
        QCOMPARE(av1.qualityLevel, 30.0); // measured
        QCOMPARE(av1.speedPreset, u"10"_s);
        // SVT-AV1 has no 4:4:4, so Best can't ask for it.
        QCOMPARE(RecordingQuality::settingsFor(RecordingQuality::Best, {}, u"libsvtav1"_s).fullColor, false);
        QCOMPARE(RecordingQuality::settingsFor(RecordingQuality::Best, {}, u"av1_nvenc"_s).fullColor, false);
        QCOMPARE(RecordingQuality::settingsFor(RecordingQuality::Best, {}, u"hevc_nvenc"_s).fullColor, true);
        QCOMPARE(RecordingQuality::settingsFor(RecordingQuality::High, {}, u"h264_nvenc"_s).speedPreset, u"p5"_s);
        QCOMPARE(RecordingQuality::settingsFor(RecordingQuality::Small, {}, u"libx265"_s).qualityLevel, 28.0);
    }

    void testCustomPassesEveryValue()
    {
        RecordingQuality::CustomValues custom;
        custom.qualityLevel = 20;
        custom.speedPreset = u"medium"_s;
        custom.keyframeSeconds = 5;
        custom.bFrames = 3;
        custom.fullColor = true;
        custom.extraOptions = u"aq-mode=3"_s;
        const auto s = RecordingQuality::settingsFor(RecordingQuality::Custom, custom);
        QCOMPARE(s.qualityLevel, 20.0);
        QCOMPARE(s.speedPreset, u"medium"_s);
        QCOMPARE(s.keyframeIntervalSeconds, 5.0);
        QCOMPARE(s.bFrames, 3);
        QCOMPARE(s.fullColor, true);
        QCOMPARE(s.extraOptions, u"aq-mode=3"_s);

        custom.bFrames = -1;
        QVERIFY(!RecordingQuality::settingsFor(RecordingQuality::Custom, custom).bFrames.has_value());
    }
};

QTEST_GUILESS_MAIN(RecordingQualityTest)
#include "RecordingQualityTest.moc"
