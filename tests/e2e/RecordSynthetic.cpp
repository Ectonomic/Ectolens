/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 *
 * Records a PipeWire video node with the settings a quality choice asks for,
 * the way Ectolens records the screen, so the result can be checked with ffprobe.
 * usage: ectolens-record-synthetic NODE_ID OUTPUT SECONDS FPS small|high|best|none
 */
#include "Platforms/RecordingQuality.h"

#include <PipeWireRecord>
#include <QCoreApplication>
#include <QTimer>
#include <cstdio>

using namespace Qt::StringLiterals;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() != 6 && args.size() != 7) {
        fprintf(stderr, "usage: %s NODE_ID OUTPUT SECONDS FPS small|high|best|none [ENCODER]\n", argv[0]);
        return 2;
    }
    const QString preset = args[5];
    const auto quality = preset == u"small" ? RecordingQuality::Small : preset == u"best" ? RecordingQuality::Best : RecordingQuality::High;

    PipeWireRecord record;
    record.setNodeId(args[1].toUInt());
    record.setEncoder(PipeWireBaseEncodedStream::H264Main);
    record.setMaxFramerate(args[4].toUInt());
    if (preset != u"none") { // "none": KPipeWire's own defaults, for comparison
        record.setEncoderSettings(RecordingQuality::settingsFor(quality, {}, args.size() == 7 ? args[6] : u"libx264"_s));
    }
    record.setOutput(args[2]);
    // ECTOLENS_E2E_AUDIO="system,mic,separate": record the default output's monitor, the
    // default input, and each on a track of its own after the mix (record-soundtracks.sh
    // runs this in a private PipeWire with stand-in devices)
    const auto audio = qEnvironmentVariable("ECTOLENS_E2E_AUDIO").split(u',', Qt::SkipEmptyParts);
    record.setRecordSystemAudio(audio.contains(u"system"_s));
    record.setRecordMicrophone(audio.contains(u"mic"_s));
    record.setSeparateAudioTracks(audio.contains(u"separate"_s));
    QObject::connect(&record, &PipeWireBaseEncodedStream::errorFound, [](const QString &e) {
        fprintf(stderr, "error: %s\n", qPrintable(e));
    });
    bool started = false;
    QObject::connect(&record, &PipeWireBaseEncodedStream::stateChanged, [&] {
        fprintf(stderr, "state %d\n", int(record.state()));
        if (record.state() != PipeWireBaseEncodedStream::Idle) {
            started = true;
        } else if (started) {
            app.quit();
        }
    });
    record.start();
    // ECTOLENS_E2E_PAUSE="AT,LENGTH" in seconds: pause at AT for LENGTH, then resume.
    const auto pauseSpec = qEnvironmentVariable("ECTOLENS_E2E_PAUSE").split(u',');
    if (pauseSpec.size() == 2) {
        const int at = int(pauseSpec[0].toDouble() * 1000), length = int(pauseSpec[1].toDouble() * 1000);
        QTimer::singleShot(at, &record, [&] {
            record.pause();
        });
        QTimer::singleShot(at + length, &record, [&] {
            record.resume();
        });
    }
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&] {
        fprintf(stderr, "dropped %d\n", record.droppedFrames());
    });
    QTimer::singleShot(int(args[3].toDouble() * 1000), &record, [&] {
        record.stop();
    });
    QTimer::singleShot(int(args[3].toDouble() * 1000) + 20000, &app, [] {
        fprintf(stderr, "timed out finishing the file\n");
        QCoreApplication::exit(1);
    });
    return app.exec();
}
