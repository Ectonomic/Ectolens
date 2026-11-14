/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#include "PrivateInfo.h"

#include <QList>
#include <QMutex>
#include <QRegion>
#include <QRegularExpression>
#include <QSysInfo>

#include <pwd.h>
#include <unistd.h>

#include <tesseract/baseapi.h>
#include <tesseract/resultiterator.h>

#include <algorithm>
#include <array>
#include <cmath>
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

// Rules read across a whole line, since the reader splits these into words. A rule with a group
// named v hides only the words under that group ("token:" stays, its value goes).
struct LineRule {
    QRegularExpression pattern;
    int minDigits = 0; // a phone number's digits, counted to tell it from other numbers
    int maxDigits = 0;
};
static const QList<LineRule> &lineRules()
{
    static const auto ci = QRegularExpression::CaseInsensitiveOption;
    static const QList<LineRule> list = {
        // phone numbers with a country code: +44 20 7946 0958, +1 (555) 867-5309, +4930123456789
        {QRegularExpression(uR"((?<![\w+])\+\d{1,3}(?:[ .\-–]?\(?\d{1,4}\)?){1,5}(?![\w\-]))"_s), 8, 15},
        // North American numbers: (555) 867-5309, 555-867-5309, 555.867.5309
        {QRegularExpression(uR"((?<![\w.\-])(?:\(\d{3}\) ?|\d{3}[ .\-–])\d{3}[ .\-–]\d{4}(?![\w\-]|\.\d))"_s), 10, 10},
        // national numbers that start with the trunk 0: 030 1234 5678, 020 7946 0958
        {QRegularExpression(uR"((?<![\w.\-:])0\d{1,4}[ /\-–]\d{3,4}(?:[ \-–]?\d{2,4})?(?![\w\-:]|\.\d))"_s), 9, 12},
        // street addresses in English: a number, one to four words, a street word, and a unit
        {QRegularExpression(uR"(\b\d{1,6}[A-Za-z]?(?: [A-Z0-9][\w.'\-]*){1,4} (?:Street|St|Avenue|Ave|Road|Rd|Boulevard|Blvd|Lane|Ln|Drive|Dr|Court|Ct|Way|Place|Pl|Parkway|Pkwy|Terrace|Ter|Circle|Cir|Highway|Hwy|Square|Sq|Trail|Trl|Crescent|Close)\b\.?(?:,? (?:Apt|Apartment|Suite|Ste|Unit|Floor|Fl|#) ?[\w\-]+)?)"_s)},
        // a street name that ends in its street word, then the number: German, Dutch and Nordic
        // (Hauptstraße 12a, Kerkstraat 5, Strandvejen 4, Drottninggatan 10, Karl Johans gate 1)
        {QRegularExpression(uR"(\b\p{Lu}[\p{L}\-]*(?:straße|strasse|str\.|weg|gasse|platz|allee|straat|laan|plein|gracht|gade|vej|vejen|gatan|gata|gate|veien|vegen|vei|katu) \d{1,4}[a-z]?\b)"_s)},
        // French: 12 rue de la Paix, 5 bis avenue Victor Hugo
        {QRegularExpression(uR"(\b\d{1,4}(?: ?(?:bis|ter))?,? (?i:rue|avenue|av\.|boulevard|bd|chemin|place|allée|impasse|quai|cours|route) (?:(?:de|du|des|la|le|l'|d')\s?)*\p{Lu}[\p{L}'\-]+(?: \p{Lu}[\p{L}'\-]+)*)"_s)},
        // Spanish, Catalan, Italian and Portuguese: Calle Mayor, 5; Via Roma 10; Rua Augusta, 100
        {QRegularExpression(uR"(\b(?:Calle|C/|Avenida|Avda\.|Plaza|Paseo|Carrer|Rua|Travessa|Praça) (?:(?:de|del|la|las|los|da|do|dos|das) )*\p{Lu}[\p{L}'\-]+(?: \p{Lu}[\p{L}'\-]+)*,? (?:n\.? ?º? ?)?\d{1,4}[a-z]?\b)"_s)},
        {QRegularExpression(uR"(\b(?:Via|Viale|Piazza|Corso|Largo) (?:(?:di|della|del|dei) )*\p{Lu}[\p{L}'\-]+(?: \p{Lu}[\p{L}'\-]+)* \d{1,4}[a-z]?\b)"_s)},
        // Polish: ul. Marszałkowska 10
        {QRegularExpression(uR"(\bul\. \p{Lu}[\p{L}\-]+(?: \p{Lu}[\p{L}\-]+)* \d{1,4}[a-z]?\b)"_s)},
        // a US city, state and ZIP code: Springfield, IL 62704
        {QRegularExpression(uR"(\b[A-Z][a-z]+(?: [A-Z][a-z]+){0,2}, [A-Z]{2} \d{5}(?:-\d{4})?\b)"_s)},
        // a UK postcode: SW1A 1AA
        {QRegularExpression(uR"(\b[A-Z]{1,2}\d[A-Z\d]? \d[A-Z]{2}\b)"_s)},
        // a Canadian postal code: K1A 0B1
        {QRegularExpression(uR"(\b[ABCEGHJ-NPRSTVXY]\d[ABCEGHJ-NPRSTV-Z] ?\d[ABCEGHJ-NPRSTV-Z]\d\b)"_s)},
        // an Australian state and postcode: NSW 2000
        {QRegularExpression(uR"(\b(?:NSW|VIC|QLD|SA|WA|TAS|NT|ACT) \d{4}\b)"_s)},
        // a Dutch postcode and its city: 1012 AB Amsterdam
        {QRegularExpression(uR"(\b\d{4} ?[A-Z]{2} \p{Lu}[\p{L}\-]+)"_s)},
        // a five-digit postcode and its city (Germany, France, Spain, Italy and others), on a line of
        // its own or after a comma, and ending the line or a part of it: 10115 Berlin, 75002 Paris
        {QRegularExpression(uR"((?:(?<=, )|^)\d{5} \p{Lu}[\p{L}\-]+(?: \p{Lu}[\p{L}\-]+)?(?=$|,))"_s)},
        // a Brazilian CEP, after "CEP" or before its city (the bare shape is a range of numbers too),
        // and a Japanese postal code after its mark: CEP 01310-100, 01310-100 São Paulo, 〒100-0001
        {QRegularExpression(uR"((?<=CEP |CEP: )\d{5}-\d{3}\b|(?<![\w\-])\d{5}-\d{3}(?= \p{Lu})|〒 ?\d{3}-\d{4})"_s)},
        // a value given by name, as secret scanners find them (Gitleaks' generic rule, detect-secrets'
        // keyword detector): password=..., "client_secret": "...", token: ... The value holds a
        // letter or a digit, so a field's ****** stays
        {QRegularExpression(uR"((?<![A-Za-z])(?:pass(?:word|wd)?|pwd|secret|token|api[_\-]?key|access[_\-]?key|private[_\-]?key|auth(?:orization)?|credentials?)["']?\s*[:=]\s*["']?(?!(?:Bearer|Basic)\b)(?<v>(?=[^\s"']*[A-Za-z0-9])[^\s"',;]{6,}))"_s, ci)},
        // and given on the command line: --password hunter22, --token=...
        {QRegularExpression(uR"((?<![\w\-])--?(?:password|passwd|token|secret|api[_\-]?key)[ =](?<v>[^\s"']{6,}))"_s, ci)},
        // an HTTP credential: Bearer ..., Basic ...
        {QRegularExpression(uR"(\b(?:Bearer|Basic)\s+(?<v>[A-Za-z0-9._~+/\-]{12,}=*))"_s)},
    };
    return list;
}

// ── the account's own names ──

static QMutex identityLock;
static std::optional<QList<QRegularExpression>> ownNames;
static QRegularExpression hideList;

// one pattern for a list of names or phrases: each a whole word or words (not inside a longer
// word), in any case, with any space between its words; the longest first, so a full name is
// one match
static QRegularExpression wholeWords(QStringList names, bool anyCase = true)
{
    if (names.isEmpty()) {
        return {};
    }
    std::sort(names.begin(), names.end(), [](const QString &a, const QString &b) {
        return a.size() > b.size();
    });
    QStringList escaped;
    for (const QString &n : std::as_const(names)) {
        escaped << QRegularExpression::escape(n).replace(u"\\ "_s, u"\\s+"_s);
    }
    return QRegularExpression(uR"((?<![\p{L}\p{N}])(?:%1)(?![\p{L}\p{N}]))"_s.arg(escaped.join(u'|')),
                              (anyCase ? QRegularExpression::CaseInsensitiveOption : QRegularExpression::NoPatternOption)
                                  | QRegularExpression::UseUnicodePropertiesOption);
}

static QList<QRegularExpression> namesPattern(const PrivateInfo::Identity &identity)
{
    // a shared or made-up account's name, or a computer named for what it is, says nothing about
    // whose screen it is
    static const QStringList generic = {u"root"_s, u"user"_s, u"admin"_s, u"administrator"_s, u"guest"_s, u"live"_s, u"liveuser"_s,
                                        u"localhost"_s, u"localdomain"_s, u"fedora"_s, u"ubuntu"_s, u"debian"_s, u"nobara"_s,
                                        u"archlinux"_s, u"linux"_s, u"steamdeck"_s, u"deck"_s, u"raspberrypi"_s, u"test"_s, u"demo"_s,
                                        u"desktop"_s, u"laptop"_s, u"workstation"_s, u"computer"_s, u"server"_s, u"home"_s, u"pc"_s};
    const auto usable = [](const QString &n) {
        return n.size() >= 3 && !generic.contains(n.toLower());
    };
    // the computer's name in any case
    QStringList anyCase;
    const QString host = identity.host.section(u'.', 0, 0);
    if (usable(host)) {
        anyCase << host;
    }
    // the name, whole or a part alone, only as a name is written: capitalized or in capitals, since
    // many names are ordinary words too ("Will Grant", "Rose": "we will grant access" stays sharp)
    const QString full = identity.fullName.section(u',', 0, 0).simplified();
    const QStringList parts = full.split(u' ', Qt::SkipEmptyParts);
    const auto capitalized = [](const QString &name) {
        QStringList words = name.split(u' ');
        for (QString &w : words) {
            w = w.left(1).toUpper() + w.mid(1);
        }
        return words.join(u' ');
    };
    QStringList asWritten;
    if (parts.size() > 1 && usable(full)) {
        asWritten << capitalized(full) << full.toUpper();
    }
    for (const QString &part : parts) {
        if (usable(part)) {
            asWritten << capitalized(part) << part.toUpper();
        }
    }
    // the login as it's typed, unless it is one of those names: then it is a word in a sentence
    // as often as a name, and paths and prompts that hold it are found on their own
    const bool loginIsAName = std::any_of(parts.cbegin(), parts.cend(), [&identity](const QString &part) {
        return part.compare(identity.login, Qt::CaseInsensitive) == 0;
    });
    if (usable(identity.login) && !loginIsAName) {
        asWritten << identity.login;
    }
    QList<QRegularExpression> patterns;
    if (!anyCase.isEmpty()) {
        patterns << wholeWords(anyCase);
    }
    // and in any case where it is joined to a file name, an address or a handle
    // (reyes-resume.pdf, will.grant@..., @dana_r), never between spaces as a word of a sentence
    QStringList glued;
    for (const QString &part : parts) {
        if (usable(part)) {
            glued << QRegularExpression::escape(part);
        }
    }
    if (!glued.isEmpty()) {
        const QString n = glued.join(u'|');
        patterns << QRegularExpression(uR"((?<=[-_./@\\])(?:%1)(?![\p{L}\p{N}])|(?<![\p{L}\p{N}])(?:%1)(?=[-_/@\\]|\.[\p{L}\p{N}]))"_s.arg(n),
                                       QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    }
    asWritten.removeDuplicates();
    if (!asWritten.isEmpty()) {
        patterns << wholeWords(asWritten, false);
    }
    return patterns;
}

PrivateInfo::Identity PrivateInfo::systemIdentity()
{
    Identity identity;
    if (const passwd *pw = getpwuid(getuid())) {
        identity.login = QString::fromLocal8Bit(pw->pw_name);
        identity.fullName = QString::fromLocal8Bit(pw->pw_gecos ? pw->pw_gecos : "");
    }
    identity.host = QSysInfo::machineHostName();
    return identity;
}

void PrivateInfo::setIdentity(const Identity &identity)
{
    QMutexLocker locker(&identityLock);
    ownNames = namesPattern(identity);
}

void PrivateInfo::setHideList(const QStringList &entries)
{
    QStringList names;
    for (const QString &entry : entries) {
        // two characters at least: one letter would blur every word it stands alone as
        const QString n = entry.simplified();
        if (n.size() >= 2 && !names.contains(n, Qt::CaseInsensitive)) {
            names << n;
        }
    }
    QMutexLocker locker(&identityLock);
    hideList = wholeWords(names);
}

static QRegularExpression hideListPattern()
{
    QMutexLocker locker(&identityLock);
    return hideList;
}

static QList<QRegularExpression> ownNamesPatterns()
{
    QMutexLocker locker(&identityLock);
    if (!ownNames) {
        ownNames = namesPattern(PrivateInfo::systemIdentity());
    }
    return *ownNames;
}

// the words of one line that a line rule finds private, the words left to right
static QList<bool> privateInLine(const QStringList &texts)
{
    QList<bool> hide(texts.size(), false);
    QString line;
    QList<int> starts;
    for (const QString &t : texts) {
        if (!line.isEmpty()) {
            line += u' ';
        }
        starts << int(line.size());
        line += t;
    }
    const auto mark = [&](int from, int to) {
        for (int k = 0; k < texts.size(); ++k) {
            if (starts[k] < to && starts[k] + texts[k].size() > from) {
                hide[k] = true;
            }
        }
    };
    for (const LineRule &rule : lineRules()) {
        auto matches = rule.pattern.globalMatch(line);
        while (matches.hasNext()) {
            const auto m = matches.next();
            if (rule.maxDigits) {
                const int digits = int(std::count_if(m.capturedView().begin(), m.capturedView().end(), [](QChar c) {
                    return c.isDigit();
                }));
                if (digits < rule.minDigits || digits > rule.maxDigits) {
                    continue;
                }
            }
            const bool part = m.capturedStart(u"v") >= 0;
            mark(int(part ? m.capturedStart(u"v") : m.capturedStart()), int(part ? m.capturedEnd(u"v") : m.capturedEnd()));
        }
    }
    // the account's own names, and what the person listed to hide
    QList<QRegularExpression> lists = ownNamesPatterns();
    lists << hideListPattern();
    for (const QRegularExpression &names : std::as_const(lists)) {
        if (names.pattern().isEmpty()) {
            continue;
        }
        auto matches = names.globalMatch(line);
        while (matches.hasNext()) {
            const auto m = matches.next();
            mark(int(m.capturedStart()), int(m.capturedEnd()));
        }
    }
    return hide;
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
    // which hidden words a terminal's wrap can carry on to the next line: the long ones a word rule
    // found (a path, a key, an address) and their own continuations, not a name or a phrase
    QList<bool> wraps(words.size(), false);
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
        wraps[i] = hide[i];
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
    // phone numbers, addresses, named secrets and the account's names, read across each line
    for (const auto &l : std::as_const(byLine)) {
        QStringList texts;
        for (const int i : l) {
            texts << words[i].text;
        }
        const QList<bool> inLine = privateInLine(texts);
        for (int k = 0; k < l.size(); ++k) {
            hide[l[k]] = hide[l[k]] || inLine[k];
        }
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
                    hide[i] = wraps[i] = true;
                    hide[j] = wraps[j] = true;
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
                        hide[pi ? j : i] = wraps[pi ? j : i] = true;
                    }
                }
            }
        }
    }
    const auto tokenish = [](const QString &t) {
        return t.size() >= 4 && !t.contains(u' ');
    };
    // a word that can be the wrapped rest of a path, key or address: a digit or one of their signs
    // inside it. A plain word ("done", "Next:") starts a line of its own, so a key that ended the
    // longest line of an editor or a page leaves it sharp
    const auto continues = [&tokenish](const QString &t) {
        static const QRegularExpression edges(u"^[\\s\"'`(\\[{<,;]+|[\\s\"'`)\\]}>,;.:!?]+$"_s);
        static const QRegularExpression signs(u"[0-9/\\\\._@=+\\-]"_s);
        const QString inner = QString(t).remove(edges);
        return tokenish(t) && inner.contains(signs);
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
                    wraps[j] = wraps[i];
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
            if (wraps[last] && atEdge && !hide[first] && continues(words[first].text)) {
                hide[first] = true;
                wraps[first] = true;
                grew = true;
            }
        }
    }
    return hide;
}

// The picture in gray for the reader, as dark words on a light background: Tesseract reads that
// best ("Improving the quality of the output": inverted images), so a dark screen is turned light.
// Small blue and light gray words on white were read in gray and missed in color, and One Dark's
// comment gray at 8 px was read only once the dark editor was turned light
// (tests/PrivateInfoTest.cpp, smallColoredTextIsRead).
static QImage forReading(const QImage &picture)
{
    QImage gray = picture.convertToFormat(QImage::Format_Grayscale8);
    // the background is the most common shade
    std::array<qint64, 256> counts{};
    for (int y = 0; y < gray.height(); ++y) {
        const uchar *row = gray.constScanLine(y);
        for (int x = 0; x < gray.width(); ++x) {
            ++counts[row[x]];
        }
    }
    if (std::max_element(counts.cbegin(), counts.cend()) - counts.cbegin() < 128) {
        gray.invertPixels();
    }
    return gray;
}

// the words the reader finds in a gray picture, their boxes in the picture's pixels divided by scale
static std::optional<QList<PrivateInfo::Word>> readWords(tesseract::TessBaseAPI &api, const QImage &gray, qreal scale)
{
    api.SetImage(gray.constBits(), gray.width(), gray.height(), gray.depth() / 8, int(gray.bytesPerLine()));
    api.SetSourceResolution(int(144 * scale));
    if (api.Recognize(nullptr) != 0) {
        return std::nullopt;
    }
    QList<PrivateInfo::Word> words;
    std::unique_ptr<tesseract::ResultIterator> it(api.GetIterator());
    if (!it) {
        return words;
    }
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
        const QRect box(QPoint(std::floor(x1 / scale), std::floor(y1 / scale)), QPoint(std::ceil(x2 / scale), std::ceil(y2 / scale)));
        words << PrivateInfo::Word{QString::fromUtf8(text.get()).trimmed(), box, line};
    } while (it->Next(tesseract::RIL_WORD));
    return words;
}

static QList<QRect> boxesToHide(const QList<PrivateInfo::Word> &words)
{
    const QList<bool> hide = PrivateInfo::wordsToHide(words);
    QList<QRect> boxes;
    for (int i = 0; i < words.size(); ++i) {
        if (hide[i]) {
            boxes << words[i].box;
        }
    }
    return boxes;
}

std::optional<QList<QRect>> PrivateInfo::findBoxes(const QImage &picture, const QString &languages)
{
    tesseract::TessBaseAPI api;
    if (api.Init(nullptr, languages.toUtf8().constData()) != 0 && api.Init(nullptr, "eng") != 0) {
        return std::nullopt;
    }
    api.SetPageSegMode(tesseract::PSM_AUTO);
    const QImage gray = forReading(picture);
    const auto first = readWords(api, gray, 1);
    if (!first) {
        return std::nullopt;
    }
    QList<QRect> boxes = boxesToHide(*first);
    // Small text gets a second, larger reading: the reader misses words much under about 20 px
    // tall (a 9 px font on a normal-density screen, read at twice its size, lost an email address
    // on white). The words' middle height says how small the text is; a picture with no words
    // read may be all small text.
    QList<int> heights;
    for (const auto &w : *first) {
        if (w.text.size() >= 2) {
            heights << w.box.height();
        }
    }
    std::sort(heights.begin(), heights.end());
    const int middle = heights.isEmpty() ? 0 : heights[heights.size() / 2];
    // words under 20 px tall are read again at about 36 px (of 30, 36, 42 and 48, 36 read the most
    // of the small and faint words in tests/PrivateInfoTest.cpp); the larger picture is held to
    // 40 million pixels, about four seconds more for a whole screen
    qreal scale = middle == 0 ? 2.0 : middle < 20 ? std::min(36.0 / middle, 3.0) : 1.0;
    scale = std::min(scale, std::sqrt(40e6 / (qreal(gray.width()) * gray.height())));
    if (scale >= 1.3) {
        const QImage larger = gray.scaled(gray.size() * scale, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        if (const auto second = readWords(api, larger, scale)) {
            boxes << boxesToHide(*second);
        }
    }
    api.End();
    // a word the reader returned twice (a long word's end again as a word inside its box, or both
    // readings with different splits) is blurred once: the largest boxes first, and a box the kept
    // ones already half cover adds nothing
    std::sort(boxes.begin(), boxes.end(), [](const QRect &a, const QRect &b) {
        return qint64(a.width()) * a.height() > qint64(b.width()) * b.height();
    });
    QList<QRect> once;
    QRegion kept;
    for (const QRect &b : std::as_const(boxes)) {
        qint64 covered = 0;
        for (const QRect &part : kept.intersected(b)) {
            covered += qint64(part.width()) * part.height();
        }
        if (covered * 2 < qint64(b.width()) * b.height()) {
            once << b;
            kept += b;
        }
    }
    return once;
}

QString PrivateInfo::hideInText(const QString &text, const QString &mark)
{
    // the words as a monospace screen would place them: a character 12 px wide on a 20 px line,
    // so a typed space reads as a real gap and never joins two words the way a reader's split does
    QStringList lines = text.split(u'\n');
    QList<Word> words;
    QList<std::pair<int, int>> where; // line, index among its parts
    for (int l = 0; l < lines.size(); ++l) {
        const QStringList parts = lines[l].split(u' ');
        int column = 0;
        for (int k = 0; k < parts.size(); ++k) {
            if (!parts[k].isEmpty()) {
                words << Word{parts[k], QRect(10 + column * 12, 10 + l * 30, int(parts[k].size()) * 12 - 2, 20), l};
                where << std::pair{l, k};
            }
            column += int(parts[k].size()) + 1;
        }
    }
    const QList<bool> hide = wordsToHide(words);
    QList<QStringList> parts;
    for (const QString &line : std::as_const(lines)) {
        parts << line.split(u' ');
    }
    for (int i = 0; i < words.size(); ++i) {
        if (hide[i]) {
            parts[where[i].first][where[i].second] = mark;
        }
    }
    for (int l = 0; l < lines.size(); ++l) {
        lines[l] = parts[l].join(u' ');
    }
    return lines.join(u'\n');
}
