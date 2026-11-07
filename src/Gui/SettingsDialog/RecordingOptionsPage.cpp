/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#include "RecordingOptionsPage.h"

#include "Platforms/RecordingQuality.h"
#include "VideoSaveOptionsPage.h"
#include "settings.h"

#include <KLocalizedString>
#include <KMessageWidget>

#include <QAction>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QRadioButton>
#include <QScreen>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <PipeWireBaseEncodedStream>
#include <PipeWireSourceStream> // Fraction

using namespace Qt::StringLiterals;

namespace
{
// What each encoder is called on the page, and where it runs.
QString encoderLabel(const QString &name)
{
    if (name == u"libx264") {
        return i18nc("@item:inlistbox encoder", "x264 on the processor");
    } else if (name == u"libx265") {
        return i18nc("@item:inlistbox encoder", "x265 (HEVC) on the processor");
    } else if (name == u"libsvtav1") {
        return i18nc("@item:inlistbox encoder", "SVT-AV1 on the processor");
    } else if (name == u"h264_nvenc") {
        return i18nc("@item:inlistbox encoder", "NVENC H.264 on the graphics card");
    } else if (name == u"hevc_nvenc") {
        return i18nc("@item:inlistbox encoder", "NVENC HEVC on the graphics card");
    } else if (name == u"av1_nvenc") {
        return i18nc("@item:inlistbox encoder", "NVENC AV1 on the graphics card");
    } else if (name.isEmpty()) {
        return i18nc("@item:inlistbox encoder", "Your system's own H.264 encoder");
    }
    return name;
}

QStringList speedPresetsFor(const QString &name)
{
    if (name == u"libsvtav1") {
        QStringList list;
        for (int i = 13; i >= 0; --i) {
            list << QString::number(i);
        }
        return list;
    }
    if (name.endsWith(u"_nvenc")) {
        return {u"p1"_s, u"p2"_s, u"p3"_s, u"p4"_s, u"p5"_s, u"p6"_s, u"p7"_s};
    }
    return {u"ultrafast"_s, u"superfast"_s, u"veryfast"_s, u"faster"_s, u"fast"_s, u"medium"_s, u"slow"_s, u"slower"_s, u"veryslow"_s};
}

// The encoders that open on this computer. Checking a graphics card's encoder
// wakes the card, so ECTOLENS_NO_GPU_PROBE=1 leaves those out (for tests).
QStringList availableEncoders()
{
    static const QStringList found = [] {
        return PipeWireBaseEncodedStream::availableEncoderNames(qEnvironmentVariable("ECTOLENS_NO_GPU_PROBE") != u"1");
    }();
    return found;
}

int screensMaxRefresh()
{
    qreal best = 60;
    for (const QScreen *screen : QGuiApplication::screens()) {
        best = std::max(best, screen->refreshRate());
    }
    return int(std::round(best));
}

QLabel *hint(const QString &text, QWidget *parent)
{
    // One line each: QFormLayout gives a wrapped label too little height.
    auto label = new QLabel(text, parent);
    QPalette p = label->palette();
    p.setColor(QPalette::WindowText, p.color(QPalette::Disabled, QPalette::WindowText));
    label->setPalette(p);
    return label;
}

QLabel *section(const QString &text, QWidget *parent)
{
    auto label = new QLabel(text, parent);
    QFont f = label->font();
    f.setBold(true);
    label->setFont(f);
    label->setContentsMargins(0, 12, 0, 2);
    return label;
}
}

RecordingOptionsPage::RecordingOptionsPage(QWidget *parent)
    : QWidget(parent)
{
    auto outer = new QVBoxLayout(this);
    outer->setContentsMargins({});

    // What recordings are right now, and how fast this computer keeps up.
    m_status = new KMessageWidget(this);
    m_status->setMessageType(KMessageWidget::Information);
    m_status->setCloseButtonVisible(false);
    m_status->setWordWrap(true);
    auto checkAction = new QAction(QIcon::fromTheme(u"speedometer"_s), i18nc("@action:button", "Check Speed"), m_status);
    connect(checkAction, &QAction::triggered, this, &RecordingOptionsPage::checkSpeed);
    m_status->addAction(checkAction);
    outer->addWidget(m_status);

    auto form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    outer->addLayout(form);

    // Quality
    form->addRow(section(i18nc("@title:group", "Quality"), this));
    m_quality = new QButtonGroup(this);
    auto qualityBox = new QVBoxLayout;
    const QList<std::pair<QString, QString>> choices = {
        {i18nc("@option:radio recording quality", "Small file"), i18nc("@info", "About 10 MB a minute of a full 3440×1440 screen with x264. Fine for chat and bug reports.")},
        {i18nc("@option:radio recording quality", "High"), i18nc("@info", "About 27 MB a minute with x264. Text stays sharp, and it plays everywhere.")},
        {i18nc("@option:radio recording quality", "Best"), i18nc("@info", "About 28 MB a minute with x264. Full color detail for editing later. Firefox and some phones can't play it.")},
        {i18nc("@option:radio recording quality", "Custom"), i18nc("@info", "Chosen for you when you change a setting under Encoder.")},
    };
    for (int i = 0; i < choices.size(); ++i) {
        auto radio = new QRadioButton(choices[i].first, this);
        m_quality->addButton(radio, i);
        qualityBox->addWidget(radio);
        auto description = hint(choices[i].second, this);
        description->setContentsMargins(radio->style()->pixelMetric(QStyle::PM_ExclusiveIndicatorWidth) + 6, 0, 0, 4);
        qualityBox->addWidget(description);
    }
    form->addRow(i18nc("@label", "Quality:"), qualityBox);

    // Picture
    form->addRow(section(i18nc("@title:group", "Picture"), this));
    m_framerate = new QSpinBox(this);
    m_framerate->setObjectName(u"kcfg_videoFramerate"_s);
    m_framerate->setRange(1, 1000);
    m_framerate->setSuffix(i18nc("@item:valuesuffix", " frames per second"));
    auto framerateRow = new QHBoxLayout;
    framerateRow->addWidget(m_framerate);
    framerateRow->addWidget(hint(i18nc("@info", "Your screen shows up to %1.", screensMaxRefresh()), this));
    form->addRow(i18nc("@label:spinbox", "Frame Rate:"), framerateRow);
    auto fullRange = new QCheckBox(i18nc("@option:check", "Full color range"), this);
    fullRange->setObjectName(u"kcfg_videoFullRange"_s);
    fullRange->setToolTip(i18nc("@info:tooltip", "Keeps every shade from black to white, but some players and websites show it washed out."));
    form->addRow(i18nc("@label", "Color Range:"), fullRange);

    // Encoder
    form->addRow(section(i18nc("@title:group", "Encoder"), this));
    m_encoder = new QComboBox(this);
    const QStringList encoders = availableEncoders();
    for (const QString &name : encoders) {
        m_encoder->addItem(encoderLabel(name), name);
    }
    // Without x264 (Fedora's and openSUSE's own FFmpeg), or with the saved encoder not found
    // here (an NVENC choice on a computer without the card), recordings use the system's own
    // H.264 encoder, as Spectacle's do: the page says so.
    if (!encoders.contains(u"libx264"_s) || !encoders.contains(RecordingQuality::encoderToUse(Settings::videoEncoder()))) {
        m_encoder->addItem(encoderLabel(QString()), QString());
    }
    auto encoderBox = new QVBoxLayout;
    encoderBox->addWidget(m_encoder);
    QStringList labels;
    for (const QString &name : encoders) {
        labels << encoderLabel(name);
    }
    auto found = hint(labels.isEmpty() ? i18nc("@info", "This computer's FFmpeg has none of x264, x265, SVT-AV1 or NVENC.")
                                       : i18nc("@info", "Found on this computer: %1.", labels.join(u", "_s)),
                      this);
    encoderBox->addWidget(found);
    form->addRow(i18nc("@label:listbox", "Encoder:"), encoderBox);

    m_speed = new QComboBox(this);
    auto speedRow = new QHBoxLayout;
    speedRow->addWidget(m_speed);
    speedRow->addWidget(hint(i18nc("@info", "Earlier in the list is faster; later makes smaller files."), this));
    form->addRow(i18nc("@label:listbox", "Speed:"), speedRow);

    m_level = new QDoubleSpinBox(this);
    m_level->setRange(0, 63);
    m_level->setDecimals(0);
    auto levelRow = new QHBoxLayout;
    levelRow->addWidget(m_level);
    levelRow->addWidget(hint(i18nc("@info", "Lower numbers are sharper and make bigger files."), this));
    form->addRow(i18nc("@label:spinbox", "Quality Level:"), levelRow);

    m_keyframes = new QDoubleSpinBox(this);
    m_keyframes->setRange(0.1, 60);
    m_keyframes->setDecimals(1);
    m_keyframes->setSuffix(i18nc("@item:valuesuffix", " seconds"));
    form->addRow(i18nc("@label:spinbox", "Keyframe Every:"), m_keyframes);

    m_bFrames = new QSpinBox(this);
    m_bFrames->setRange(-1, 16);
    m_bFrames->setSpecialValueText(i18nc("@item:inrange", "Chosen by the speed"));
    form->addRow(i18nc("@label:spinbox", "B-Frames:"), m_bFrames);

    m_fullColor = new QCheckBox(i18nc("@option:check", "Full color detail (4:4:4)"), this);
    m_fullColor->setToolTip(i18nc("@info:tooltip", "Sharper colored text. Firefox and some phones can't play it, and SVT-AV1 and NVENC AV1 can't record it."));
    form->addRow(i18nc("@label", "Color Detail:"), m_fullColor);

    m_extra = new QLineEdit(this);
    m_extra->setPlaceholderText(i18nc("@info:placeholder", "Options in the encoder's own words, such as bframes=3"));
    m_extra->setMinimumWidth(340);
    form->addRow(i18nc("@label:textbox", "Extra Options:"), m_extra);

    // Sound
    form->addRow(section(i18nc("@title:group", "Sound"), this));
    auto systemAudio = new QCheckBox(i18nc("@option:check", "Record system audio"), this);
    systemAudio->setObjectName(u"kcfg_videoRecordSystemAudio"_s);
    auto microphone = new QCheckBox(i18nc("@option:check", "Record microphone"), this);
    microphone->setObjectName(u"kcfg_videoRecordMicrophone"_s);
    // Ectolens: with both, a mixed track first (every player plays it) and each source alone
    // after it, for editing; one source is one track either way
    auto separate = new QCheckBox(i18nc("@option:check", "Separate tracks for editing"), this);
    separate->setObjectName(u"kcfg_videoSeparateAudioTracks"_s);
    auto separateHint = hint(i18nc("@info", "Track 1 mixes both, so players play both; tracks 2 and 3 hold each alone."), this);
    auto soundBox = new QVBoxLayout;
    soundBox->addWidget(systemAudio);
    soundBox->addWidget(microphone);
    soundBox->addWidget(separate);
    soundBox->addWidget(separateHint);
    form->addRow(i18nc("@label", "Sound:"), soundBox);
    const auto bothSources = [systemAudio, microphone, separate, separateHint] {
        const bool both = systemAudio->isChecked() && microphone->isChecked();
        separate->setEnabled(both);
        separateHint->setEnabled(both);
    };
    connect(systemAudio, &QCheckBox::toggled, this, bothSources);
    connect(microphone, &QCheckBox::toggled, this, bothSources);
    QMetaObject::invokeMethod(this, bothSources, Qt::QueuedConnection); // once the saved values are shown

    // File: the existing Video Saving page (location, filename, format)
    form->addRow(section(i18nc("@title:group", "File"), this));
    m_filePage = new VideoSaveOptionsPage(this);
    outer->addWidget(m_filePage);
    outer->addWidget(hint(i18nc("@info", "MP4 files are written in pieces as they record, so a crash can't ruin the file."), this));
    outer->addStretch();

    connect(m_quality, &QButtonGroup::idClicked, this, &RecordingOptionsPage::choosePreset);
    connect(m_encoder, &QComboBox::activated, this, [this] {
        const int preset = m_quality->checkedId();
        fillSpeedPresets(m_encoder->currentData().toString());
        if (preset != RecordingQuality::Custom) {
            choosePreset(preset); // that choice's numbers for this encoder
        } else {
            m_speed->setCurrentIndex(std::max(0, m_speed->findText(u"veryfast"_s)));
            updateStatus();
            Q_EMIT changed();
        }
    });
    for (auto spin : {m_level, m_keyframes}) {
        connect(spin, &QDoubleSpinBox::valueChanged, this, &RecordingOptionsPage::markCustom);
    }
    connect(m_bFrames, &QSpinBox::valueChanged, this, &RecordingOptionsPage::markCustom);
    connect(m_speed, &QComboBox::activated, this, &RecordingOptionsPage::markCustom);
    connect(m_fullColor, &QCheckBox::toggled, this, &RecordingOptionsPage::markCustom);
    connect(m_extra, &QLineEdit::textEdited, this, &RecordingOptionsPage::markCustom);
    connect(m_framerate, &QSpinBox::valueChanged, this, &RecordingOptionsPage::updateStatus);

    load();
}

RecordingOptionsPage::Values RecordingOptionsPage::current() const
{
    Values v;
    v.quality = m_quality->checkedId();
    v.encoder = m_encoder->currentData().toString();
    if (v.quality == RecordingQuality::Custom) {
        v.speed = m_speed->currentText();
        v.level = m_level->value();
        v.keyframes = m_keyframes->value();
        v.bFrames = m_bFrames->value();
        v.fullColor = m_fullColor->isChecked();
        v.extra = m_extra->text();
    }
    return v;
}

RecordingOptionsPage::Values RecordingOptionsPage::saved() const
{
    Values v;
    v.quality = Settings::videoQuality();
    // what a recording would use (RecordingQuality::encoderToUse); an encoder Settings
    // didn't find here falls to the system's own, or the first found
    v.encoder = RecordingQuality::encoderToUse(Settings::videoEncoder());
    if (m_encoder->findData(v.encoder) < 0) {
        v.encoder = m_encoder->findData(QString()) >= 0 ? QString() : m_encoder->itemData(0).toString();
    }
    if (v.quality == RecordingQuality::Custom) {
        v.speed = Settings::videoSpeedPreset();
        v.level = Settings::videoQualityLevel();
        v.keyframes = Settings::videoKeyframeSeconds();
        v.bFrames = Settings::videoBFrames();
        v.fullColor = Settings::videoFullColor();
        v.extra = Settings::videoExtraOptions();
    }
    return v;
}

void RecordingOptionsPage::display(const Values &v)
{
    m_syncing = true;
    m_encoder->setCurrentIndex(std::max(0, m_encoder->findData(v.encoder)));
    fillSpeedPresets(v.encoder);
    if (auto button = m_quality->button(v.quality)) {
        button->setChecked(true);
    }
    m_syncing = false;
    if (v.quality != RecordingQuality::Custom) {
        choosePreset(v.quality);
        return;
    }
    m_syncing = true;
    if (m_speed->findText(v.speed) < 0) {
        m_speed->addItem(v.speed); // a preset name typed into the config by hand
    }
    m_speed->setCurrentText(v.speed);
    m_level->setValue(v.level);
    m_keyframes->setValue(v.keyframes);
    m_bFrames->setValue(v.bFrames);
    m_fullColor->setChecked(v.fullColor);
    m_extra->setText(v.extra);
    m_syncing = false;
    updateStatus();
}

bool RecordingOptionsPage::hasChanges() const
{
    return !(current() == saved());
}

bool RecordingOptionsPage::isDefault() const
{
    const Values v = current();
    return v.quality == RecordingQuality::High && v.encoder == RecordingQuality::encoderToUse(u"libx264"_s);
}

void RecordingOptionsPage::save()
{
    const Values v = current();
    Settings::setVideoQuality(v.quality);
    Settings::setVideoEncoder(v.encoder);
    if (v.quality == RecordingQuality::Custom) {
        // The custom values are kept only for Custom; the other choices carry their own.
        Settings::setVideoSpeedPreset(v.speed);
        Settings::setVideoQualityLevel(v.level);
        Settings::setVideoKeyframeSeconds(v.keyframes);
        Settings::setVideoBFrames(v.bFrames);
        Settings::setVideoFullColor(v.fullColor);
        Settings::setVideoExtraOptions(v.extra);
    }
    Settings::self()->save();
}

void RecordingOptionsPage::load()
{
    m_speedResult.clear();
    display(saved());
}

void RecordingOptionsPage::applyDefaults()
{
    Values v;
    v.quality = RecordingQuality::High;
    v.encoder = RecordingQuality::encoderToUse(u"libx264"_s);
    display(v);
    Q_EMIT changed();
}

void RecordingOptionsPage::choosePreset(int preset)
{
    if (preset == RecordingQuality::Custom) {
        updateStatus();
        Q_EMIT changed();
        return;
    }
    // Show the exact values a choice means, so the numbers are never hidden.
    const auto s = RecordingQuality::settingsFor(static_cast<RecordingQuality::Preset>(preset), {}, m_encoder->currentData().toString());
    m_syncing = true;
    m_level->setValue(s.qualityLevel.value_or(16));
    m_speed->setCurrentText(s.speedPreset);
    m_keyframes->setValue(s.keyframeIntervalSeconds.value_or(2));
    m_bFrames->setValue(s.bFrames.value_or(-1));
    m_fullColor->setChecked(s.fullColor);
    m_extra->clear();
    m_syncing = false;
    m_speedResult.clear();
    updateStatus();
    Q_EMIT changed();
}

void RecordingOptionsPage::markCustom()
{
    if (m_syncing) {
        return;
    }
    m_quality->button(RecordingQuality::Custom)->setChecked(true);
    m_speedResult.clear();
    updateStatus();
    Q_EMIT changed();
}

void RecordingOptionsPage::fillSpeedPresets(const QString &encoderName)
{
    const bool wasSyncing = m_syncing;
    m_syncing = true;
    m_speed->clear();
    m_speed->addItems(speedPresetsFor(encoderName));
    m_syncing = wasSyncing;
}
void RecordingOptionsPage::updateStatus()
{
    const int preset = m_quality->checkedId();
    const QString quality = preset == RecordingQuality::Small ? i18nc("@info quality name in a sentence", "Small file")
        : preset == RecordingQuality::Best                    ? i18nc("@info quality name in a sentence", "Best")
        : preset == RecordingQuality::Custom                  ? i18nc("@info quality name in a sentence", "Custom")
                                                              : i18nc("@info quality name in a sentence", "High");
    // The quality choices reach x264, x265, SVT-AV1 and NVENC; KPipeWire's VA-API and
    // OpenH264 encoders, the system's own, keep their own settings.
    QString text = m_encoder->currentData().toString().isEmpty()
        ? i18nc("@info %1 frames per second",
                "Recordings are %1 frames per second, encoded by your system's own H.264 encoder at its own quality. The quality choices apply to x264, x265, SVT-AV1 and NVENC.",
                m_framerate->value())
        : i18nc("@info %1 quality, %2 frames per second, %3 encoder",
                "Recordings are %1 quality at %2 frames per second, encoded by %3.",
                quality,
                m_framerate->value(),
                encoderLabel(m_encoder->currentData().toString()));
    if (!m_speedResult.isEmpty()) {
        text += u' ' + m_speedResult;
    }
    m_status->setText(text);
}

void RecordingOptionsPage::checkSpeed()
{
    if (m_encoder->currentData().toString().isEmpty()) {
        // the system's own encoder is KPipeWire's choice at the time of recording; nothing to time here
        m_speedResult.clear();
        updateStatus();
        return;
    }
    // The settings as shown on the page, not as last saved.
    RecordingQuality::CustomValues custom;
    custom.qualityLevel = m_level->value();
    custom.speedPreset = m_speed->currentText();
    custom.keyframeSeconds = m_keyframes->value();
    custom.bFrames = m_bFrames->value();
    custom.fullColor = m_fullColor->isChecked();
    custom.extraOptions = m_extra->text();
    const auto settings = RecordingQuality::settingsFor(static_cast<RecordingQuality::Preset>(m_quality->checkedId()), custom, m_encoder->currentData().toString());
    QSize size(1920, 1080);
    if (const QScreen *screen = QGuiApplication::primaryScreen()) {
        size = screen->size() * screen->devicePixelRatio();
    }
    const quint32 fps = m_framerate->value();
    m_speedResult = i18nc("@info", "Checking speed…");
    updateStatus();
    auto watcher = new QFutureWatcher<PipeWireBaseEncodedStream::SpeedCheck>(this);
    connect(watcher, &QFutureWatcher<PipeWireBaseEncodedStream::SpeedCheck>::finished, this, [this, watcher, size] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (!result.ok) {
            m_speedResult = result.error;
        } else if (result.realtimeFactor >= 1.2) {
            m_speedResult = i18nc("@info %1 width, %2 height, %3 factor",
                                  "On a %1×%2 screen this computer encodes %3 times faster than it needs to.",
                                  size.width(),
                                  size.height(),
                                  QString::number(result.realtimeFactor, 'f', 1));
        } else {
            m_speedResult = i18nc("@info %1 width, %2 height, %3 factor",
                                  "On a %1×%2 screen this computer only keeps up at %3 times the speed needed, so frames would be dropped. Choose a faster speed, a lower frame rate or another encoder.",
                                  size.width(),
                                  size.height(),
                                  QString::number(result.realtimeFactor, 'f', 2));
        }
        updateStatus();
    });
    watcher->setFuture(QtConcurrent::run([settings, size, fps] {
        return PipeWireBaseEncodedStream::checkEncodingSpeed(settings, size, Fraction{.numerator = fps, .denominator = 1}, int(std::max<quint32>(fps * 2, 60)));
    }));
}

#include "moc_RecordingOptionsPage.cpp"
