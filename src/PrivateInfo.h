/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#pragma once

#include <QImage>
#include <QList>
#include <QRect>
#include <QString>
#include <QStringList>

#include <optional>

/**
 * Ectolens: Hide Private Info's rule (design proposal 2, section 05). A word read from the
 * picture is private when it is an email address, a key or a token (a known start like
 * sk-, ghp_, AKIA, a web token, or a long run of letters and digits mixed together), or a
 * home-folder path. Read across a whole line, so they count when the reader splits them
 * into words: phone numbers, street addresses and postcodes, the value given to a key,
 * token, secret or password by name (password=..., token: ..., Bearer ...), and the name,
 * login and computer name of the account Ectolens runs in. tests/PrivateInfoTest.cpp holds
 * it to examples both ways, and to words read by the text reader from drawn screens,
 * small and faint text among them.
 */
namespace PrivateInfo
{
bool looksPrivate(const QString &word);

/// The account whose own names are private: its full name, login and computer name.
struct Identity {
    QString fullName; ///< as the account gives it; anything after a comma is dropped
    QString login;
    QString host;
};
/// This account's names, read from the system.
Identity systemIdentity();
/// The names to hide from now on (the system's to start with; tests give a made-up one).
void setIdentity(const Identity &identity);

/**
 * What the person adds in Settings › Private Information: names, places, project names or
 * anything else, blurred wherever it appears as a whole word or words, in any case.
 */
void setHideList(const QStringList &entries);

/// The text with each private word replaced by the mark, lines and spacing kept.
QString hideInText(const QString &text, const QString &mark = QStringLiteral("[hidden]"));

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
