/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#pragma once

#include <QImage>
#include <QList>
#include <QRect>
#include <QString>

#include <optional>

/**
 * Ectolens: Hide Private Info's rule (design proposal 2, section 05). A word read from the
 * picture is private when it is an email address, a key or a token (a known start like
 * sk-, ghp_, AKIA, a web token, or a long run of letters and digits mixed together), or a
 * home-folder path. tests/PrivateInfoTest.cpp holds it to examples both ways, and to words
 * read by the text reader from a drawn terminal.
 */
namespace PrivateInfo
{
bool looksPrivate(const QString &word);

/// A word as the text reader found it: its text, its box in the picture, and its line.
struct Word {
    QString text;
    QRect box;
    int line = 0;
};

/**
 * Which of the words to blur: the private ones; two neighbors that read as private only
 * when joined (a key the reader split with a space); words touching a private one with no
 * real gap; and a private word's own continuation when it ran to the text's right edge and
 * went on at the start of the next line (a wrapped path or key). A word that is not private
 * is never blurred for merely standing next to one.
 */
QList<bool> wordsToHide(const QList<Word> &words);

/**
 * Reads an RGB888 picture with Tesseract and returns the boxes to blur, in the picture's own
 * pixels. Empty optional when the text reader could not start.
 */
std::optional<QList<QRect>> findBoxes(const QImage &picture, const QString &languages);
}
