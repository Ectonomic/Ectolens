/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#include "PrivateInfo.h"

#include <QList>
#include <QRegularExpression>

#include <tesseract/baseapi.h>
#include <tesseract/resultiterator.h>

#include <algorithm>
#include <memory>

using namespace Qt::StringLiterals;

static const QList<QRegularExpression> &rules()
{
    static const QList<QRegularExpression> list = {
        // email addresses
        QRegularExpression(u"[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(\\.[A-Za-z0-9-]+)*\\.[A-Za-z]{2,}"_s),
        // home folders: /home/name, /Users/name, ~/…, C:\Users\name
        QRegularExpression(u"(^|[^A-Za-z0-9])(/home/|/Users/|~/|[A-Za-z]:\\\\Users\\\\)"_s),
        // the same with whatever the text reader glued before them: Claude Code's ⎿ mark and the
        // slash read as "f#home/dana/..." (seen in an agent capture)
        QRegularExpression(u"(home|Users)/[A-Za-z0-9._-]{2,}"_s),
        // a home folder written as a folder name, as Claude Code names its projects
        // (~/.claude/projects/-home-dana-dev-app): at a word's or a folder's start, so a flag like
        // --home-dir stays readable (2026-11-04)
        QRegularExpression(u"(^|/)-(home|Users)-[A-Za-z0-9._]{2,}"_s),
        // a shell prompt or an scp-style address, name@computer: (a prompt at home,
        // dana@workstation:~$, came through an agent capture; it names the account and the machine)
        QRegularExpression(u"^[a-z_][a-z0-9_.-]*@[A-Za-z0-9][A-Za-z0-9.-]*:"_s),
        // keys and tokens with a known start
        QRegularExpression(u"(sk|pk|rk)[-_](live|test|proj|ant)?[-_]?[A-Za-z0-9_-]{12,}"_s),
        QRegularExpression(u"(ghp|gho|ghu|ghs|ghr)_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,}|glpat-[A-Za-z0-9_-]{16,}"_s),
        QRegularExpression(u"xox[abposr]-[A-Za-z0-9-]{10,}|AKIA[0-9A-Z]{16}|AIza[0-9A-Za-z_-]{30,}"_s),
        // a JSON web token: three base64url parts, the first starting eyJ
        QRegularExpression(u"eyJ[A-Za-z0-9_-]{8,}\\.[A-Za-z0-9_-]{8,}\\.[A-Za-z0-9_-]{8,}"_s),
    };
    return list;
}

bool PrivateInfo::looksPrivate(const QString &rawWord)
{
    // Trim what OCR leaves around a word.
    static const QRegularExpression edges(u"^[\\s\"'`(\\[{<,;]+|[\\s\"'`)\\]}>,;.:!?]+$"_s);
    const QString word = QString(rawWord).remove(edges);
    if (word.size() < 4) {
        return false;
    }
    for (const auto &rule : rules()) {
        if (rule.match(word).hasMatch()) {
            return true;
        }
    }
    // Any other long run of letters and digits mixed together reads as a key or a token:
    // 24 or more characters, with at least three of each.
    static const QRegularExpression token(u"^[A-Za-z0-9_+/=-]{24,}$"_s);
    if (token.match(word).hasMatch()) {
        int digits = 0;
        int letters = 0;
        for (const QChar c : word) {
            digits += c.isDigit();
            letters += c.isLetter();
        }
        return digits >= 3 && letters >= 3;
    }
    return false;
}

QList<bool> PrivateInfo::wordsToHide(const QList<Word> &words)
{
    QList<bool> hide(words.size(), false);
    int lines = 0;
    // a word the picture's left edge cut: a home folder read as "ome/dana/..." (2026-11-04, the
    // agent run's area across the terminal's pwd line)
    const auto cutHome = [](const QString &word) {
        for (const QString &start : {u"/home/"_s, u"/Users/"_s}) {
            for (int k = 1; k + 2 <= start.size(); ++k) {
                const QString rest = start.mid(k);
                if (word.startsWith(rest) && word.size() >= rest.size() + 2) {
                    return true;
                }
            }
        }
        return false;
    };
    for (int i = 0; i < words.size(); ++i) {
        hide[i] = looksPrivate(words[i].text) || (words[i].box.left() <= 3 && cutHome(words[i].text));
        lines = std::max(lines, words[i].line + 1);
    }
    // the words of each line, left to right
    QList<QList<int>> byLine(lines);
    for (int i = 0; i < words.size(); ++i) {
        byLine[words[i].line] << i;
    }
    for (auto &l : byLine) {
        std::sort(l.begin(), l.end(), [&words](int a, int b) {
            return words[a].box.left() < words[b].box.left();
        });
    }
    // two neighbors that read as private only together: "sk-proj-9fK2 xQ7mL4" is one token.
    // When either is private alone, joining proves nothing about the other: a path makes any
    // neighbor look private joined to it ("Bash(cd" before "/home/dana/...", 2026-11-04).
    // When one is private alone, the other is taken in only when a rule's match on the two joined
    // runs into the other's own letters: "dana." + "reyes@example.com" is one address, while in
    // "Bash(cd" + "/home/dana/..." the match starts inside the path.
    for (const auto &l : std::as_const(byLine)) {
        for (int a = 0; a + 1 < l.size(); ++a) {
            const int i = l[a], j = l[a + 1];
            const QString &left = words[i].text, &right = words[j].text;
            const bool pi = looksPrivate(left), pj = looksPrivate(right);
            const QString joined = left + right;
            if (!pi && !pj) {
                if (looksPrivate(joined)) {
                    hide[i] = true;
                    hide[j] = true;
                }
                continue;
            }
            if (pi == pj) {
                continue;
            }
            // and only across a gap the reader made, not a typed space: in the agent run the reader
            // split an email and a key with gaps of 7 to 13 px on lines 26 to 34 px tall, while real
            // spaces on the same screen left 18 px or more
            const QRect ba = words[i].box, bb = words[j].box;
            if (bb.left() - ba.right() >= std::max(ba.height(), bb.height()) * 0.5) {
                continue;
            }
            // the other word's characters in the joined text
            const int from = pi ? int(left.size()) : 0, to = pi ? int(joined.size()) : int(left.size());
            const int need = std::max(2, (to - from + 1) / 2);
            for (const auto &rule : rules()) {
                auto matches = rule.globalMatch(joined);
                while (matches.hasNext()) {
                    const auto m = matches.next();
                    const int overlap = std::min(to, int(m.capturedEnd())) - std::max(from, int(m.capturedStart()));
                    if (overlap >= need) {
                        hide[pi ? j : i] = true;
                    }
                }
            }
        }
    }
    const auto tokenish = [](const QString &t) {
        return t.size() >= 4 && !t.contains(u' ');
    };
    // the text's right edge, where a terminal wraps a long line
    int right = 0;
    for (const auto &w : words) {
        right = std::max(right, w.box.right());
    }
    bool grew = true;
    while (grew) {
        grew = false;
        // take in words that touch a hidden one on its line: no real gap between them, or boxes
        // that overlap (the reader can return a long word's end as a second word inside its box)
        for (int i = 0; i < words.size(); ++i) {
            if (!hide[i]) {
                continue;
            }
            for (int j = 0; j < words.size(); ++j) {
                if (hide[j] || words[j].line != words[i].line) {
                    continue;
                }
                const QRect a = words[i].box, b = words[j].box;
                const int gap = b.left() > a.right() ? b.left() - a.right() : a.left() > b.right() ? a.left() - b.right() : -1;
                if (gap < std::max(a.height(), b.height()) * 0.2 && tokenish(words[j].text)) {
                    hide[j] = true;
                    grew = true;
                }
            }
        }
        // a hidden word that ran to the right edge goes on at the start of the next line: a terminal
        // wraps a long path or key there, and the rest of it is as private (an agent
        // capture blurred a path's first line and sent its wrapped end sharp)
        for (int l = 0; l + 1 < lines; ++l) {
            if (byLine[l].isEmpty() || byLine[l + 1].isEmpty()) {
                continue;
            }
            // the line's last word is the one reaching furthest right
            const int last = *std::max_element(byLine[l].cbegin(), byLine[l].cend(), [&words](int a, int b) {
                return words[a].box.right() < words[b].box.right();
            });
            const int first = byLine[l + 1].first();
            const Word &a = words[last];
            // within about two characters of the edge (a character is about 0.6 of the line's height)
            const bool atEdge = a.box.right() >= right - a.box.height() * 1.2;
            if (hide[last] && atEdge && !hide[first] && tokenish(words[first].text)) {
                hide[first] = true;
                grew = true;
            }
        }
    }
    return hide;
}

std::optional<QList<QRect>> PrivateInfo::findBoxes(const QImage &picture, const QString &languages)
{
    const QImage rgb = picture.convertToFormat(QImage::Format_RGB888);
    tesseract::TessBaseAPI api;
    if (api.Init(nullptr, languages.toUtf8().constData()) != 0 && api.Init(nullptr, "eng") != 0) {
        return std::nullopt;
    }
    api.SetPageSegMode(tesseract::PSM_AUTO);
    api.SetImage(rgb.constBits(), rgb.width(), rgb.height(), 3, int(rgb.bytesPerLine()));
    api.SetSourceResolution(144);
    if (api.Recognize(nullptr) != 0) {
        return std::nullopt;
    }
    QList<QRect> boxes;
    std::unique_ptr<tesseract::ResultIterator> it(api.GetIterator());
    if (!it) {
        return boxes;
    }
    QList<Word> words;
    int line = 0;
    do {
        if (it->IsAtBeginningOf(tesseract::RIL_TEXTLINE) && !words.isEmpty()) {
            ++line;
        }
        std::unique_ptr<char[]> text(it->GetUTF8Text(tesseract::RIL_WORD));
        int x1, y1, x2, y2;
        if (!text || !it->BoundingBox(tesseract::RIL_WORD, &x1, &y1, &x2, &y2)) {
            continue;
        }
        words << Word{QString::fromUtf8(text.get()).trimmed(), QRect(QPoint(x1, y1), QPoint(x2, y2)), line};
    } while (it->Next(tesseract::RIL_WORD));
    const QList<bool> hide = wordsToHide(words);
    for (int i = 0; i < words.size(); ++i) {
        if (hide[i]) {
            boxes << words[i].box;
        }
    }
    api.End();
    return boxes;
}
