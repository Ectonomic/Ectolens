/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#include "Gui/SettingsDialog/PrivateInfoOptionsPage.h"
#include "PrivateInfo.h"
#include "PrivateInfoSettings.h"
#include "settings.h"

#include <KConfig>
#include <KConfigDialogManager>
#include <KConfigGroup>
#include <KEditListWidget>

#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QStandardPaths>
#include <QTest>

using namespace Qt::StringLiterals;

// Settings › Private Information from the outside: a name typed into the page's list and added
// with its Add button, saved the way the settings dialog's OK saves, is blurred by Hide Private
// Info's rule and kept in the settings file; removed, it is sharp again.
class HideListTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        // a made-up account, so the test never depends on whose computer runs it
        PrivateInfo::setIdentity({u"Dana Reyes"_s, u"dana"_s, u"bluefin"_s});
        Settings::setAlwaysHide({});
        Settings::self()->save();
        PrivateInfoSettings::follow();
    }

    void aNameAddedInSettingsIsBlurred()
    {
        const QString line = u"Call with Priya Natarajan about Bluebird"_s;
        QCOMPARE(PrivateInfo::hideInText(line), line);

        PrivateInfoOptionsPage page;
        KConfigDialogManager manager(&page, Settings::self());
        manager.updateWidgets();
        auto list = page.findChild<KEditListWidget *>(u"kcfg_alwaysHide"_s);
        QVERIFY(list);
        for (const QString &entry : {u"Priya Natarajan"_s, u"Bluebird"_s}) {
            list->lineEdit()->setFocus();
            QTest::keyClicks(list->lineEdit(), entry);
            QVERIFY(list->addButton()->isEnabled());
            QTest::mouseClick(list->addButton(), Qt::LeftButton);
        }
        // the list puts a new entry above the chosen one; the order means nothing to the blur
        const auto sorted = [](QStringList l) {
            l.sort();
            return l;
        };
        QCOMPARE(sorted(list->items()), (QStringList{u"Bluebird"_s, u"Priya Natarajan"_s}));
        QVERIFY(manager.hasChanged());
        manager.updateSettings(); // the dialog's OK

        QCOMPARE(sorted(Settings::alwaysHide()), (QStringList{u"Bluebird"_s, u"Priya Natarajan"_s}));
        QCOMPARE(PrivateInfo::hideInText(line), u"Call with [hidden] [hidden] about [hidden]"_s);
        const KConfig saved(u"ectolensrc"_s);
        QCOMPARE(sorted(saved.group(u"General"_s).readEntry("alwaysHide", QStringList())), (QStringList{u"Bluebird"_s, u"Priya Natarajan"_s}));

        // removed with its Remove button: sharp again
        list->listView()->setCurrentIndex(list->listView()->model()->index(int(list->items().indexOf(u"Bluebird"_s)), 0));
        QTest::mouseClick(list->removeButton(), Qt::LeftButton);
        manager.updateSettings();
        QCOMPARE(PrivateInfo::hideInText(line), u"Call with [hidden] [hidden] about Bluebird"_s);
    }
};

QTEST_MAIN(HideListTest)

#include "HideListTest.moc"
