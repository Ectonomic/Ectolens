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


// a screen drawn for the text reader: each line in Geist Mono, and each word's cell
struct DrawnScreen {
    QImage picture;
    QList<std::pair<QString, QRect>> words;
};
static DrawnScreen drawScreen(const QStringList &lines, int pixelSize, QColor ink, QColor background)
{
    const int id = QFontDatabase::addApplicationFont(QStringLiteral(ECTOLENS_FONTS_DIR "/GeistMono-Regular.ttf"));
    QFont font(QFontDatabase::applicationFontFamilies(id).value(0));
    font.setPixelSize(pixelSize);
    const QFontMetrics metrics(font);
    const int advance = metrics.horizontalAdvance(u'M');
    const int lineHeight = metrics.height() + pixelSize / 3;
    int columns = 0;
    for (const auto &l : lines) {
        columns = std::max(columns, int(l.size()));
    }
    DrawnScreen s;
    s.picture = QImage(2 * pixelSize + columns * advance, 2 * pixelSize + int(lines.size()) * lineHeight, QImage::Format_RGB888);
    s.picture.fill(background);
    QPainter painter(&s.picture);
    painter.setFont(font);
    painter.setPen(ink);
    for (int l = 0; l < lines.size(); ++l) {
        const int top = pixelSize + l * lineHeight;
        painter.drawText(QPoint(pixelSize, top + metrics.ascent()), lines[l]);
        int column = 0;
        for (const auto &w : lines[l].split(u' ')) {
            if (!w.isEmpty()) {
                s.words << std::pair{w, QRect(pixelSize + column * advance, top, int(w.size()) * advance, metrics.height())};
            }
            column += int(w.size()) + 1;
        }
    }
    return s;
}
// how much of each private word's ink the blurs cover, and the most any other word loses
static QString blurReport(const DrawnScreen &s, const QStringList &secret, qreal padding)
{
    const auto boxes = PrivateInfo::findBoxes(s.picture, u"eng"_s);
    if (!boxes) {
        return u"the text reader did not start"_s;
    }
    QRegion blurred;
    for (const QRect &b : *boxes) {
        blurred += b.adjusted(-padding, -padding, padding, padding);
    }
    const int background = qGray(s.picture.pixel(0, 0));
    QStringList problems;
    // each word blurred once: no blur mostly under the others (stacked blurs read as more items
    // than there are, and blur softer than one)
    for (int i = 0; i < boxes->size(); ++i) {
        QRegion others;
        for (int j = 0; j < boxes->size(); ++j) {
            if (j != i) {
                others += boxes->at(j);
            }
        }
        qint64 area = 0;
        for (const QRect &part : others.intersected(boxes->at(i))) {
            area += qint64(part.width()) * part.height();
        }
        if (area * 2 >= qint64(boxes->at(i).width()) * boxes->at(i).height()) {
            problems << u"a blur at %1,%2 is mostly under another"_s.arg(boxes->at(i).x()).arg(boxes->at(i).y());
        }
    }
    for (const auto &[text, cell] : s.words) {
        QRect inkBox;
        for (int y = cell.top(); y <= cell.bottom(); ++y) {
            for (int x = cell.left(); x <= cell.right(); ++x) {
                if (std::abs(qGray(s.picture.pixel(x, y)) - background) > 12) {
                    inkBox |= QRect(x, y, 1, 1);
                }
            }
        }
        if (inkBox.isEmpty()) {
            continue;
        }
        qint64 area = 0;
        for (const QRect &part : blurred.intersected(inkBox)) {
            area += qint64(part.width()) * part.height();
        }
        const double c = double(area) / (qint64(inkBox.width()) * inkBox.height());
        if (secret.contains(text) && c < 0.98) {
            problems << u"%1 is only %2% blurred"_s.arg(text).arg(int(c * 100));
        } else if (!secret.contains(text) && c > 0.15) {
            problems << u"%1 is %2% blurred, and it is not private"_s.arg(text).arg(int(c * 100));
        }
    }
    return problems.join(u"; "_s);
}

// Hide Private Info blurs what this rule calls private. Words a person shares on purpose
// (commands, file names, ordinary words and numbers) must stay readable.
class PrivateInfoTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase()
    {
        // a made-up account, so the test never depends on whose computer runs it
        PrivateInfo::setIdentity({u"Dana Reyes"_s, u"dana"_s, u"bluefin"_s});
        PrivateInfo::setHideList({});
    }

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
        // (another account's folder, so the account's own name doesn't hide the second one)
        PrivateInfo::Word cut{u"ome/sam/projects/launch-notes"_s, QRect(0, 10, 360, 20), 0};
        PrivateInfo::Word inside{u"ome/sam/projects/launch-notes"_s, QRect(200, 40, 360, 20), 1};
        QCOMPARE(hidden({cut, inside}), (QStringList{u"ome/sam/projects/launch-notes"_s}));
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


    // ── what the reader must see: small text, faint text ──
    // (the addresses here aren't the made-up account's, so only the reading can find them)

    void smallTextIsRead()
    {
        // an 8 px terminal font on a normal-density screen, read at twice its size: 16 px
        const DrawnScreen s = drawScreen({u"$ git config user.email"_s, u"sam.okafor@example.com"_s, u"$ cat .env"_s, u"STRIPE=sk_live_51HxQ2bLkY8mN3pR7tV"_s},
                                         16, QColor(0xfc, 0xfc, 0xfc), QColor(0x23, 0x26, 0x27));
        const QString problems = blurReport(s, {u"sam.okafor@example.com"_s, u"STRIPE=sk_live_51HxQ2bLkY8mN3pR7tV"_s}, 6);
        QVERIFY2(problems.isEmpty(), qPrintable(problems));
    }

    void faintTextIsRead()
    {
        // a comment's gray on a dark editor (One Dark's #5c6370 on #282c34), read at twice its size
        const DrawnScreen s = drawScreen({u"# deploy notes"_s, u"# owner: sam.okafor@example.com"_s, u"# key: sk-proj-9fK2xQ7mL4vR8tY1wZ3bN6cD5hJ0"_s},
                                         28, QColor(0x5c, 0x63, 0x70), QColor(0x28, 0x2c, 0x34));
        const QString problems = blurReport(s, {u"sam.okafor@example.com"_s, u"sk-proj-9fK2xQ7mL4vR8tY1wZ3bN6cD5hJ0"_s}, 6);
        QVERIFY2(problems.isEmpty(), qPrintable(problems));
    }

    void faintTextOnLightIsRead()
    {
        // light gray on white, as a web page's fine print
        const DrawnScreen s = drawScreen({u"Signed in as sam.okafor@example.com"_s, u"Version 6.11.2, updated on November 14"_s}, 28, QColor(0xb0, 0xb0, 0xb0), QColor(0xff, 0xff, 0xff));
        const QString problems = blurReport(s, {u"sam.okafor@example.com"_s}, 6);
        QVERIFY2(problems.isEmpty(), qPrintable(problems));
    }

    // ── private across several words: phone numbers, addresses, a key named by its keyword, your name ──

    void privateLines_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<QString>("expected");
        QTest::newRow("us phone") << u"call (555) 867-5309 today"_s << u"call [hidden] [hidden] today"_s;
        QTest::newRow("us phone dashes") << u"Phone: 555-867-5309"_s << u"Phone: [hidden]"_s;
        QTest::newRow("us phone dots") << u"cell 555.867.5309."_s << u"cell [hidden]"_s;
        QTest::newRow("international") << u"+44 20 7946 0958 (office)"_s << u"[hidden] [hidden] [hidden] [hidden] (office)"_s;
        QTest::newRow("international one block") << u"tel:+4930123456789"_s << u"[hidden]"_s;
        QTest::newRow("national with trunk zero") << u"Tel. 030 1234 5678"_s << u"Tel. [hidden] [hidden] [hidden]"_s;
        QTest::newRow("street address") << u"Ship to 1428 Elm Street, Apt 4B"_s << u"Ship to [hidden] [hidden] [hidden] [hidden] [hidden]"_s;
        QTest::newRow("numbered street") << u"350 5th Ave"_s << u"[hidden] [hidden] [hidden]"_s;
        QTest::newRow("city state zip") << u"Springfield, IL 62704-1234"_s << u"[hidden] [hidden] [hidden]"_s;
        QTest::newRow("uk postcode") << u"London SW1A 1AA"_s << u"London [hidden] [hidden]"_s;
        QTest::newRow("german street") << u"Hauptstraße 12a, Berlin"_s << u"[hidden] [hidden] Berlin"_s;
        QTest::newRow("german street and postcode") << u"Lindenweg 5, 10115 Berlin"_s << u"[hidden] [hidden] [hidden] [hidden]"_s;
        QTest::newRow("dutch street and postcode") << u"Kerkstraat 12, 1012 AB Amsterdam"_s << u"[hidden] [hidden] [hidden] [hidden] [hidden]"_s;
        QTest::newRow("danish street") << u"Strandvejen 4"_s << u"[hidden] [hidden]"_s;
        QTest::newRow("swedish street") << u"Drottninggatan 10, Stockholm"_s << u"[hidden] [hidden] Stockholm"_s;
        QTest::newRow("french street") << u"12 rue de la Paix"_s << u"[hidden] [hidden] [hidden] [hidden] [hidden]"_s;
        QTest::newRow("french postcode") << u"75002 Paris"_s << u"[hidden] [hidden]"_s;
        QTest::newRow("spanish street") << u"Calle Mayor, 5"_s << u"[hidden] [hidden] [hidden]"_s;
        QTest::newRow("italian street") << u"Via Roma 10"_s << u"[hidden] [hidden] [hidden]"_s;
        QTest::newRow("portuguese street") << u"Rua Augusta, 100"_s << u"[hidden] [hidden] [hidden]"_s;
        QTest::newRow("polish street") << u"ul. Marszałkowska 10"_s << u"[hidden] [hidden] [hidden]"_s;
        QTest::newRow("canadian postal code") << u"Ottawa ON K1A 0B1"_s << u"Ottawa ON [hidden] [hidden]"_s;
        QTest::newRow("australian postcode") << u"Sydney NSW 2000"_s << u"Sydney [hidden] [hidden]"_s;
        QTest::newRow("brazilian cep") << u"CEP 01310-100"_s << u"CEP [hidden]"_s;
        QTest::newRow("brazilian cep before its city") << u"01310-100 São Paulo"_s << u"[hidden] São Paulo"_s;
        QTest::newRow("japanese postal code") << u"〒100-0001 Tokyo"_s << u"[hidden] Tokyo"_s;
        QTest::newRow("password assignment") << u"export DB_PASSWORD=hunter22"_s << u"export [hidden]"_s;
        QTest::newRow("token after a colon") << u"token: 9f8e7d6c"_s << u"token: [hidden]"_s;
        QTest::newRow("quoted secret") << u"\"client_secret\": \"aB3dE6gH\","_s << u"\"client_secret\": [hidden]"_s;
        QTest::newRow("bearer") << u"Authorization: Bearer abc123def456ghi"_s << u"Authorization: Bearer [hidden]"_s;
        QTest::newRow("password flag") << u"mysql --password hunter22 -h db"_s << u"mysql --password [hidden] -h db"_s;
        QTest::newRow("your full name") << u"Author: Dana Reyes"_s << u"Author: [hidden] [hidden]"_s;
        QTest::newRow("your last name") << u"reyes-resume.pdf"_s << u"[hidden]"_s;
        QTest::newRow("your login") << u"Signed in as Dana"_s << u"Signed in as [hidden]"_s;
        QTest::newRow("your computer") << u"ssh bluefin.local"_s << u"ssh [hidden]"_s;
        QTest::newRow("your name in capitals") << u"REYES, DANA"_s << u"[hidden] [hidden]"_s;
    }
    void privateLines()
    {
        QFETCH(QString, line);
        QFETCH(QString, expected);
        QCOMPARE(PrivateInfo::hideInText(line), expected);
    }

    void publicLines_data()
    {
        QTest::addColumn<QString>("line");
        QTest::newRow("diff stat") << u"3 files changed, 120 insertions(+), 15 deletions(-)"_s;
        QTest::newRow("date and time") << u"2026-11-14 05:30:12 started"_s;
        QTest::newRow("resolution") << u"1920 1080 60 Hz"_s;
        QTest::newRow("version") << u"Version 6.11.2 (build 4821)"_s;
        QTest::newRow("ip address") << u"inet 192.168.1.10/24"_s;
        QTest::newRow("sizes") << u"about 10, 27 or 28 MB"_s;
        QTest::newRow("prose about passwords") << u"Enter your password below"_s;
        QTest::newRow("password field label") << u"Password: ******"_s;
        QTest::newRow("key name alone") << u"OPENAI_API_KEY is not set"_s;
        QTest::newRow("oauth setting") << u"oauth: enabled"_s;
        QTest::newRow("steps") << u"Step 2 Open Settings"_s;
        QTest::newRow("a name that is not yours") << u"Author: Sam Lee"_s;
        QTest::newRow("a word holding your name") << u"Danalyzer reyesian"_s;
        QTest::newRow("a computer named for what it is") << u"my workstation and laptop"_s;
        QTest::newRow("a count and a word") << u"10000 Files processed"_s;
        QTest::newRow("a count at a line's end") << u"Done: 12345 Items"_s;
        QTest::newRow("sent via a service") << u"Sent via Slack to 3 people"_s;
        QTest::newRow("a year and a word") << u"2026 AB testing"_s;
        QTest::newRow("a range of numbers") << u"pages 12345-678"_s;
    }
    void publicLines()
    {
        QFETCH(QString, line);
        QCOMPARE(PrivateInfo::hideInText(line), line);
    }

    void yourNameAsAnOrdinaryWordStaysSharp()
    {
        // first and last names that are ordinary words too: hidden as names, not as words
        PrivateInfo::setIdentity({u"Will Grant"_s, u"will"_s, u"bluefin"_s});
        const QString sentence = u"We will grant access, and it will expire."_s;
        const QString asName = PrivateInfo::hideInText(u"Will Grant approved it; ask GRANT"_s);
        const QString words = PrivateInfo::hideInText(sentence);
        const QString paths = PrivateInfo::hideInText(u"/home/will/notes will@bluefin:~$ grant-resume.pdf will.grant@example.org"_s);
        PrivateInfo::setIdentity({u"Dana Reyes"_s, u"dana"_s, u"bluefin"_s});
        QCOMPARE(words, sentence);
        QCOMPARE(asName, u"[hidden] [hidden] approved it; ask [hidden]"_s);
        QCOMPARE(paths, u"[hidden] [hidden] [hidden] [hidden]"_s);
        // a login that isn't the name is hidden as typed
        PrivateInfo::setIdentity({u"Dana Reyes"_s, u"dreyes"_s, u"bluefin"_s});
        const QString login = PrivateInfo::hideInText(u"Signed in as dreyes"_s);
        PrivateInfo::setIdentity({u"Dana Reyes"_s, u"dana"_s, u"bluefin"_s});
        QCOMPARE(login, u"Signed in as [hidden]"_s);
    }

    void yourNameComesFromTheAccount()
    {
        // the system's account gives the name to hide; a shared account's generic name is left out
        PrivateInfo::setIdentity({u"Sam Lee,Room 4,,"_s, u"root"_s, u"localhost"_s});
        QCOMPARE(PrivateInfo::hideInText(u"Sam Lee as root on localhost"_s), u"[hidden] [hidden] as root on localhost"_s);
        PrivateInfo::setIdentity({u"Dana Reyes"_s, u"dana"_s, u"bluefin"_s});
        QCOMPARE(PrivateInfo::hideInText(u"Sam Lee"_s), u"Sam Lee"_s);
        const auto system = PrivateInfo::systemIdentity();
        QVERIFY(!system.login.isEmpty());
    }


    void smallColoredTextIsRead_data()
    {
        // each one the reader missed before the gray, second reading, light-turned reading (2026-11-14)
        QTest::addColumn<int>("size");
        QTest::addColumn<QColor>("ink");
        QTest::addColumn<QColor>("background");
        QTest::newRow("8 px white on dark") << 8 << QColor(0xfc, 0xfc, 0xfc) << QColor(0x23, 0x26, 0x27);
        QTest::newRow("8 px string green on dark") << 8 << QColor(0x98, 0xc3, 0x79) << QColor(0x28, 0x2c, 0x34);
        QTest::newRow("9 px comment gray on dark") << 9 << QColor(0x5c, 0x63, 0x70) << QColor(0x28, 0x2c, 0x34);
        // read only once the dark editor is turned light (2026-11-14)
        QTest::newRow("8 px comment gray on dark") << 8 << QColor(0x5c, 0x63, 0x70) << QColor(0x28, 0x2c, 0x34);
        QTest::newRow("10 px comment gray on dark") << 10 << QColor(0x5c, 0x63, 0x70) << QColor(0x28, 0x2c, 0x34);
        QTest::newRow("8 px link blue on white") << 8 << QColor(0x40, 0x78, 0xf2) << QColor(0xff, 0xff, 0xff);
        QTest::newRow("8 px fine print gray on white") << 8 << QColor(0x9a, 0xa0, 0xa6) << QColor(0xff, 0xff, 0xff);
        QTest::newRow("8 px error red on pink") << 8 << QColor(0xc0, 0x39, 0x2b) << QColor(0xfd, 0xec, 0xea);
    }
    void smallColoredTextIsRead()
    {
        // a small font on a normal-density screen, enlarged to twice its size as Hide Private Info
        // reads it
        QFETCH(int, size);
        QFETCH(QColor, ink);
        QFETCH(QColor, background);
        DrawnScreen s = drawScreen({u"$ git config user.email"_s, u"sam.okafor@example.com"_s, u"STRIPE=sk_live_51HxQ2bLkY8mN3pR7tV"_s,
                                    u"done in 3.2 seconds, all good, nothing left to do"_s},
                                   size, ink, background);
        s.picture = s.picture.scaled(s.picture.size() * 2, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        for (auto &w : s.words) {
            w.second = QRect(w.second.topLeft() * 2, w.second.size() * 2);
        }
        const QString problems = blurReport(s, {u"sam.okafor@example.com"_s, u"STRIPE=sk_live_51HxQ2bLkY8mN3pR7tV"_s}, 6);
        QVERIFY2(problems.isEmpty(), qPrintable(problems));
    }


    // ── the person's own list (Settings › Private Information) ──

    void hideListBlursWhatThePersonAdded_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<QString>("expected");
        QTest::newRow("a phrase") << u"Ship Project Falcon by Friday"_s << u"Ship [hidden] [hidden] by Friday"_s;
        QTest::newRow("in any case and spacing") << u"PROJECT  falcon"_s << u"[hidden]  [hidden]"_s;
        QTest::newRow("a name") << u"Meeting with Sam Lee"_s << u"Meeting with [hidden] [hidden]"_s;
        QTest::newRow("beside punctuation") << u"(acme), acme.com"_s << u"[hidden] [hidden]"_s;
        QTest::newRow("with signs of its own") << u"the C++ team"_s << u"the [hidden] [hidden]"_s;
        QTest::newRow("not inside a longer word") << u"Acmeville and Falconry"_s << u"Acmeville and Falconry"_s;
        QTest::newRow("a one-letter entry is left out") << u"x marks the spot"_s << u"x marks the spot"_s;
        QTest::newRow("a blank entry is left out") << u"two  spaces"_s << u"two  spaces"_s;
    }
    void hideListBlursWhatThePersonAdded()
    {
        QFETCH(QString, line);
        QFETCH(QString, expected);
        PrivateInfo::setHideList({u"Project Falcon"_s, u"Sam Lee"_s, u"Acme"_s, u"C++ team"_s, u"x"_s, u"  "_s});
        QCOMPARE(PrivateInfo::hideInText(line), expected);
        PrivateInfo::setHideList({});
        QCOMPARE(PrivateInfo::hideInText(line), line);
    }

    void hideListIsReadOffTheScreen()
    {
        // through the real text reader: names the patterns can't know, blurred once listed
        const DrawnScreen s = drawScreen({u"Meeting notes"_s, u"With Sam Okafor from Northwind"_s, u"Next: the Falcon launch review"_s}, 28,
                                         QColor(0xfc, 0xfc, 0xfc), QColor(0x23, 0x26, 0x27));
        const QStringList listed = {u"Sam"_s, u"Okafor"_s, u"Northwind"_s, u"Falcon"_s};
        QVERIFY2(blurReport(s, listed, 6).contains(u"Northwind is only 0% blurred"_s), "the names were blurred before they were listed");
        PrivateInfo::setHideList({u"Sam Okafor"_s, u"Northwind"_s, u"Falcon"_s});
        const QString problems = blurReport(s, listed, 6);
        PrivateInfo::setHideList({});
        QVERIFY2(problems.isEmpty(), qPrintable(problems));
    }
    void aNameAtTheEdgeDoesNotTakeTheNextLine()
    {
        // a name the person listed ends the longest line: the next line's first word is not its
        // wrapped end (only a path, key or address runs on past a terminal's edge)
        PrivateInfo::setHideList({u"Northwind"_s});
        const QList<PrivateInfo::Word> words = {
            at(0, 0, u"With"_s), at(0, 5, u"Sam"_s), at(0, 9, u"from"_s), at(0, 14, u"Northwind"_s),
            at(1, 0, u"Next:"_s), at(1, 6, u"review"_s),
        };
        const QStringList h = hidden(words);
        PrivateInfo::setHideList({});
        QCOMPARE(h, (QStringList{u"Northwind"_s}));
    }

    void aKeyAtTheEdgeLeavesAPlainNextLine()
    {
        // a key ends the longest line of an editor or a page: the next line's first word is a word of
        // its own, not the key's wrapped end
        const QList<PrivateInfo::Word> words = {
            at(0, 0, u"STRIPE=sk_live_51HxQ2bLkY8mN3pR7tV"_s),
            at(1, 0, u"done"_s), at(1, 5, u"in"_s), at(1, 8, u"3.2"_s), at(1, 12, u"seconds."_s),
        };
        QCOMPARE(hidden(words), (QStringList{u"STRIPE=sk_live_51HxQ2bLkY8mN3pR7tV"_s}));
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
