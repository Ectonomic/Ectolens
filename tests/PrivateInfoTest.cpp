/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#include "PrivateInfo.h"

#include <QFontDatabase>
#include <QFontMetrics>
#include <QPainter>
#include <QRegion>
#include <QTest>

using namespace Qt::StringLiterals;

// a word box on a 20 px line, a character 12 px wide: line, first column, text
static PrivateInfo::Word at(int line, int column, const QString &text)
{
    return {text, QRect(10 + column * 12, 10 + line * 30, int(text.size()) * 12 - 2, 20), line};
}
static QStringList hidden(const QList<PrivateInfo::Word> &words)
{
    const QList<bool> hide = PrivateInfo::wordsToHide(words);
    QStringList out;
    for (int i = 0; i < words.size(); ++i) {
        if (hide[i]) {
            out << words[i].text;
        }
    }
    return out;
}

// Hide Private Info blurs what this rule calls private. Words a person shares on purpose
// (commands, file names, ordinary words and numbers) must stay readable.
class PrivateInfoTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void privateWords_data()
    {
        QTest::addColumn<QString>("word");
        QTest::newRow("email") << u"dana.reyes@example.com"_s;
        QTest::newRow("email with plus") << u"dana+ectolens@example.org"_s;
        QTest::newRow("email in quotes") << u"\"ops@company.io\","_s;
        QTest::newRow("home path") << u"/home/dana/projects/launch-notes"_s;
        QTest::newRow("mac home") << u"/Users/dana/Desktop"_s;
        QTest::newRow("tilde path") << u"~/projects/app"_s;
        QTest::newRow("prompt with home") << u"dana@workstation:~/projects$"_s;
        QTest::newRow("prompt at home") << u"dana@workstation:~$"_s;
        QTest::newRow("home read with a mark glued on") << u"f#home/dana/.local/opt/app"_s;
        QTest::newRow("home after a letter") << u"L/home/dana/projects"_s;
        QTest::newRow("root prompt") << u"root@buildbox:/srv#"_s;
        QTest::newRow("scp address") << u"dana@nas.local:backups/"_s;
        QTest::newRow("claude project folder") << u"-home-dana-dev-ectolens"_s;
        QTest::newRow("claude project folder in a path") << u".claude/projects/-home-dana/notes.jsonl"_s;
        QTest::newRow("windows home") << u"C:\\Users\\dana\\Documents"_s;
        QTest::newRow("openai key") << u"sk-proj-9fK2xQ7mL4vR8tY1wZ3bN6cD5hJ0"_s;
        QTest::newRow("stripe key") << u"sk_live_51HxQ2bLkY8mN3pR7tV"_s;
        QTest::newRow("github token") << u"ghp_A1b2C3d4E5f6G7h8I9j0K1l2M3n4O5p6Q7r8"_s;
        QTest::newRow("aws key id") << u"AKIAIOSFODNN7EXAMPLE"_s;
        QTest::newRow("slack token") << u"xoxb-123456789012-abcdefABCDEF"_s;
        QTest::newRow("jwt") << u"eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiIxMjM0In0.dBjftJeZ4CVPmB92K27uhbUJU1p1r_wW1gFWFOEjXk"_s;
        QTest::newRow("long mixed token") << u"a8f3K29dLq0Zx7Vb4Nm1Rt6Yp"_s;
    }
    void privateWords()
    {
        QFETCH(QString, word);
        QVERIFY2(PrivateInfo::looksPrivate(word), qPrintable(word));
    }

    void publicWords_data()
    {
        QTest::addColumn<QString>("word");
        QTest::newRow("command") << u"git"_s;
        QTest::newRow("flag") << u"--hide-menubar"_s;
        QTest::newRow("file") << u"budget.ods"_s;
        QTest::newRow("relative path") << u"tests/e2e/record-synthetic.sh"_s;
        QTest::newRow("system path") << u"/usr/share/applications"_s;
        QTest::newRow("word") << u"Screenshots"_s;
        QTest::newRow("long word") << u"internationalization"_s;
        QTest::newRow("homework") << u"src/homework.txt"_s;
        QTest::newRow("home alone") << u"home"_s;
        QTest::newRow("home flag") << u"--home-dir"_s;
        QTest::newRow("home in a name") << u"src/at-home-page.qml"_s;
        QTest::newRow("number") << u"1920x1080"_s;
        QTest::newRow("version") << u"6.11.2"_s;
        QTest::newRow("url") << u"https://ectonomic.com"_s;
        QTest::newRow("at sign alone") << u"@"_s;
        QTest::newRow("handle") << u"@dana"_s;
        QTest::newRow("hex color") << u"#C8F22E"_s;
        QTest::newRow("snake case name") << u"OPENAI_API_KEY"_s;
        QTest::newRow("long lowercase identifier") << u"record_synthetic_quality_presets"_s;
    }
    void publicWords()
    {
        QFETCH(QString, word);
        QVERIFY2(!PrivateInfo::looksPrivate(word), qPrintable(word));
    }
    // ── which words get blurred, from the words and boxes the text reader gives ──

    void neighborsOfAPrivateWordStaySharp()
    {
        // An agent capture: "Bash(cd" and "(15" were blurred with the paths
        // beside them
        const QList<PrivateInfo::Word> words = {
            at(0, 0, u"Bash(cd"_s), at(0, 8, u"/home/dana/.claude/projects/"_s), at(0, 37, u"&&"_s), at(0, 40, u"grep"_s),
            at(1, 0, u"Read"_s), at(1, 5, u"/home/dana/notes/summary.txt"_s), at(1, 34, u"(15"_s), at(1, 38, u"lines)"_s),
        };
        QCOMPARE(hidden(words), (QStringList{u"/home/dana/.claude/projects/"_s, u"/home/dana/notes/summary.txt"_s}));
    }

    void anEmailSplitWhereOneHalfIsPrivateIsHiddenWhole()
    {
        // the reader split the agent run's email after "dana."; "reyes@example.com" is private alone
        // read with a small gap (the reader's split), then a real space before "notes"
        // (7 px on a 20 px line, as measured: wider than "touching", narrower than a space)
        PrivateInfo::Word dana = at(0, 0, u"dana."_s), reyes = at(0, 5, u"reyes@example.com"_s);
        reyes.box.moveLeft(dana.box.right() + 7);
        const QList<PrivateInfo::Word> words = {dana, reyes, at(0, 24, u"notes"_s)};
        QCOMPARE(hidden(words), (QStringList{u"dana."_s, u"reyes@example.com"_s}));
    }

    void aHomeFolderCutByThePicturesEdgeIsHidden()
    {
        // the frame's left edge cut "/home/dana/projects" to "ome/dana/projects"
        PrivateInfo::Word cut{u"ome/dana/projects/launch-notes"_s, QRect(0, 10, 360, 20), 0};
        PrivateInfo::Word inside{u"ome/dana/projects/launch-notes"_s, QRect(200, 40, 360, 20), 1};
        QCOMPARE(hidden({cut, inside}), (QStringList{u"ome/dana/projects/launch-notes"_s}));
        QCOMPARE(PrivateInfo::wordsToHide({cut, inside}), (QList<bool>{true, false}));
    }

    void aKeySplitByASpaceIsHiddenWhole()
    {
        const QList<PrivateInfo::Word> words = {at(0, 0, u"echo"_s), at(0, 5, u"sk-proj-9fK2"_s), at(0, 18, u"xQ7mL4vR8tY1"_s)};
        QCOMPARE(hidden(words), (QStringList{u"sk-proj-9fK2"_s, u"xQ7mL4vR8tY1"_s}));
    }

    void aWrappedPathIsHiddenOnBothLines()
    {
        // the path runs to the right edge (column 80) and goes on at the next line's start
        const QList<PrivateInfo::Word> words = {
            at(0, 2, u"Read"_s), at(0, 7, u"/home/dana/.cache/claude-tmp/claude-1000/-home-dana/ea287f6e-df6"_s),
            at(1, 5, u"-fdf3392f76d5/tasks/b44okoxq3.output"_s), at(1, 42, u"(7"_s), at(1, 45, u"lines)"_s),
            at(2, 2, u"Read"_s), at(2, 7, u"src/Gui/EctoButton.qml"_s), at(2, 30, u"(107"_s), at(2, 35, u"lines)"_s),
        };
        QCOMPARE(hidden(words),
                 (QStringList{u"/home/dana/.cache/claude-tmp/claude-1000/-home-dana/ea287f6e-df6"_s, u"-fdf3392f76d5/tasks/b44okoxq3.output"_s}));
    }

    void aPrivateWordShortOfTheEdgeDoesNotTakeTheNextLine()
    {
        const QList<PrivateInfo::Word> words = {
            at(0, 0, u"$"_s), at(0, 2, u"pwd"_s), at(0, 40, u"a-much-longer-line-sets-the-edge-here"_s),
            at(1, 0, u"/home/dana/projects"_s),
            at(2, 0, u"notes.md"_s), at(2, 10, u"budget.ods"_s),
        };
        QCOMPARE(hidden(words), (QStringList{u"/home/dana/projects"_s}));
    }

    // ── the same, read by the real text reader from a drawn terminal ──

    void aDrawnTerminalIsBlurredWordByWord()
    {
        const int id = QFontDatabase::addApplicationFont(QStringLiteral(ECTOLENS_FONTS_DIR "/GeistMono-Regular.ttf"));
        QVERIFY(id >= 0);
        QFont font(QFontDatabase::applicationFontFamilies(id).value(0));
        font.setPixelSize(28); // a 14 px terminal font, read at twice its size as Hide Private Info reads it
        const QFontMetrics metrics(font);
        const int advance = metrics.horizontalAdvance(u'M');
        const int lineHeight = metrics.height() + 8;
        // Claude Code's output in a terminal: the second line's path runs to the edge and wraps
        const QStringList lines = {
            u"* Bash(cd /home/dana/.claude/projects/ && grep -o 'sent3' notes.jsonl)"_s,
            u"  Read /home/dana/.cache/claude-tmp/claude-1000/-home-dana/ea287f6e-df6e-4fbc"_s,
            u"     -fdf3392f76d5/tasks/b44okoxq3.output (7 lines)"_s,
            u"  Read src/Gui/EctoButton.qml (107 lines)"_s,
        };
        int columns = 0;
        for (const auto &l : lines) {
            columns = std::max(columns, int(l.size()));
        }
        QCOMPARE(int(lines[1].size()), columns); // the wrapped line is the one at the edge
        QImage picture(40 + columns * advance, 40 + int(lines.size()) * lineHeight, QImage::Format_RGB888);
        picture.fill(QColor(0x23, 0x26, 0x27));
        QPainter painter(&picture);
        painter.setFont(font);
        painter.setPen(QColor(0xfc, 0xfc, 0xfc));
        struct Drawn {
            QString text;
            QRect box;
        };
        QList<Drawn> drawn;
        for (int l = 0; l < lines.size(); ++l) {
            const int top = 20 + l * lineHeight;
            painter.drawText(QPoint(20, top + metrics.ascent()), lines[l]);
            int column = 0;
            for (const auto &w : lines[l].split(u' ')) {
                if (!w.isEmpty()) {
                    drawn << Drawn{w, QRect(20 + column * advance, top, int(w.size()) * advance, metrics.height())};
                }
                column += int(w.size()) + 1;
            }
        }
        painter.end();
        const auto boxes = PrivateInfo::findBoxes(picture, u"eng"_s);
        QVERIFY2(boxes.has_value(), "the text reader did not start");
        // each blur as Ectolens lays it: the word's box with 3 logical pixels around it (6 here, at 2x)
        QRegion blurred;
        for (const QRect &b : *boxes) {
            blurred += b.adjusted(-6, -6, 6, 6);
        }
        // a word's ink: the drawn pixels inside its cell
        const auto ink = [&picture](const QRect &cell) {
            QRect r;
            for (int y = cell.top(); y <= cell.bottom(); ++y) {
                for (int x = cell.left(); x <= cell.right(); ++x) {
                    if (qGray(picture.pixel(x, y)) > 0x80) {
                        r |= QRect(x, y, 1, 1);
                    }
                }
            }
            return r;
        };
        const auto covered = [&blurred](const QRect &r) {
            qint64 area = 0;
            for (const QRect &part : blurred.intersected(r)) {
                area += qint64(part.width()) * part.height();
            }
            return double(area) / (qint64(r.width()) * r.height());
        };
        const QStringList secret = {u"/home/dana/.claude/projects/"_s, u"/home/dana/.cache/claude-tmp/claude-1000/-home-dana/ea287f6e-df6e-4fbc"_s,
                                    u"-fdf3392f76d5/tasks/b44okoxq3.output"_s};
        // the same terminal with its left edge through the second line's home folder, as an agent's
        // area can cut it: "/home/dana/.cache/..." begins "me/dana/.cache/..."
        {
            const int cutAt = 20 + int(lines[1].indexOf(u"/home/"_s) + 3) * advance;
            const QImage cut = picture.copy(cutAt, 0, picture.width() - cutAt, picture.height());
            const auto cutBoxes = PrivateInfo::findBoxes(cut, u"eng"_s);
            QVERIFY(cutBoxes.has_value());
            const QRect path = drawn[9].box.adjusted(-cutAt, 0, -cutAt, 0).intersected(cut.rect());
            QCOMPARE(drawn[9].text, u"/home/dana/.cache/claude-tmp/claude-1000/-home-dana/ea287f6e-df6e-4fbc"_s);
            QRegion cutBlurred;
            for (const QRect &b : *cutBoxes) {
                cutBlurred += b.adjusted(-6, -6, 6, 6);
            }
            qint64 area = 0;
            for (const QRect &part : cutBlurred.intersected(path)) {
                area += qint64(part.width()) * part.height();
            }
            QVERIFY2(area >= qint64(path.width()) * path.height() * 0.9, "the home folder cut by the picture's edge was sent sharp");
        }
        for (const auto &d : std::as_const(drawn)) {
            const double c = covered(ink(d.box));
            if (secret.contains(d.text)) {
                QVERIFY2(c >= 0.98, qPrintable(u"%1 is only %2% blurred"_s.arg(d.text).arg(int(c * 100))));
            } else {
                // a word beside a blur can lose its edge to the blur's padding, not more
                QVERIFY2(c <= 0.15, qPrintable(u"%1 is %2% blurred, and it is not private"_s.arg(d.text).arg(int(c * 100))));
            }
        }
    }
};


QTEST_MAIN(PrivateInfoTest)

#include "PrivateInfoTest.moc"
