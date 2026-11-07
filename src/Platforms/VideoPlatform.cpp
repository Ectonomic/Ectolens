/* This file is part of Spectacle, the KDE screenshot utility
 * SPDX-FileCopyrightText: 2019 Boudhayan Gupta <bgupta@kde.org>

 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "VideoPlatform.h"
#include <QTimerEvent>

using namespace Qt::StringLiterals;

VideoPlatform::VideoPlatform(QObject *parent)
    : QObject(parent)
{
}

bool VideoPlatform::isRecording() const
{
    // Ectolens: a paused recording is still a recording (the overlay stays, Print Screen stops it).
    return m_recordingState == RecordingState::Recording || m_recordingState == RecordingState::Paused;
}

bool VideoPlatform::isFinishing() const
{
    return m_recordingState == RecordingState::Rendering;
}

qint64 VideoPlatform::recordedTime() const
{
    return m_recordedTime;
}

void VideoPlatform::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == m_basicTimer.timerId()) {
        m_recordedTime = m_elapsedTimer.isValid() ? m_elapsedTimer.elapsed() - m_pausedTime : 0;
        Q_EMIT recordedTimeChanged();
    }
}

QString VideoPlatform::extensionForFormat(Format format)
{
    switch (format) {
    case WebM_VP9: return "webm"_L1;
    case MP4_H264: return "mp4"_L1;
    case WebP: return "webp"_L1;
    case Gif: return "gif"_L1;
    default: return {};
    }
}

VideoPlatform::Format VideoPlatform::formatForExtension(const QString &extension)
{
    auto lowercaseExtension = extension.toLower();
    if (lowercaseExtension == "webm"_L1) {
        return WebM_VP9;
    } else if (lowercaseExtension == "mp4"_L1) {
        return MP4_H264;
    } else if (lowercaseExtension == "webp"_L1) {
        return WebP;
    } else if (lowercaseExtension == "gif"_L1) {
        return Gif;
    } else {
        return NoFormat;
    }
}

VideoPlatform::Format VideoPlatform::formatForPath(const QString &path)
{
    return formatForExtension(path.mid(path.lastIndexOf(u'.') + 1));
}

bool VideoPlatform::formatSupportsAudio(VideoPlatform::Format format)
{
    return format == WebM_VP9 || format == MP4_H264;
}

VideoPlatform::RecordingState VideoPlatform::recordingState() const
{
    return m_recordingState;
}

VideoPlatform::RecordingMode VideoPlatform::recordingMode() const
{
    return m_recordingMode;
}

void VideoPlatform::setRecordingState(RecordingState state)
{
    if (state == m_recordingState) {
        return;
    }

    const RecordingState previous = m_recordingState;
    m_recordingState = state;
    if (state == RecordingState::NotRecording) {
        m_recordedTime = 0;
        m_pausedTime = 0;
        m_elapsedTimer.invalidate();
        m_basicTimer.stop();
    } else if (state == RecordingState::Recording && previous == RecordingState::Paused) {
        // Ectolens: resuming. The paused stretch is left out of the file, so it
        // is left out of the time shown too.
        m_pausedTime += m_pauseTimer.elapsed();
        m_pauseTimer.invalidate();
        m_basicTimer.start(1000, Qt::PreciseTimer, this);
    } else if (state == RecordingState::Recording) {
        m_recordedTime = 0;
        m_pausedTime = 0;
        m_elapsedTimer.start();
        m_basicTimer.start(1000, Qt::PreciseTimer, this);
    } else if (state == RecordingState::Paused) {
        m_recordedTime = m_elapsedTimer.isValid() ? m_elapsedTimer.elapsed() - m_pausedTime : m_recordedTime;
        m_pauseTimer.start();
        m_basicTimer.stop();
    } else {
        if (m_elapsedTimer.isValid()) {
            m_recordedTime = m_elapsedTimer.elapsed() - m_pausedTime - (m_pauseTimer.isValid() ? m_pauseTimer.elapsed() : 0);
        }
        m_elapsedTimer.invalidate();
        m_pauseTimer.invalidate();
        m_basicTimer.stop();
    }
    if (state != RecordingState::Recording && state != RecordingState::Paused) {
        setRecordingMode(NoRecordingModes);
    }
    m_recordingState = state;
    Q_EMIT recordingStateChanged(state);
    Q_EMIT recordedTimeChanged();
}

void VideoPlatform::setRecordingMode(RecordingMode mode)
{
    if (m_recordingMode == mode) {
        return;
    }
    m_recordingMode = mode;
}

#include "moc_VideoPlatform.cpp"
