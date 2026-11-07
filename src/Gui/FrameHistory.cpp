/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#include "FrameHistory.h"

#include "CaptureTools.h"
#include "Selection.h"
#include "SelectionEditor.h"
#include "../SpectacleCore.h"

#include <KQuickImageEditor/AnnotationDocument>

#include <QGuiApplication>

static FrameHistory *s_instance = nullptr;
// how long the frame must stay still for a change to become one step
static constexpr int s_settleMs = 400;

FrameHistory *FrameHistory::instance()
{
    if (!s_instance) {
        s_instance = new FrameHistory(qApp);
    }
    return s_instance;
}

FrameHistory::FrameHistory(QObject *parent)
    : QObject(parent)
{
    m_settle.setSingleShot(true);
    m_settle.setInterval(s_settleMs);
    connect(&m_settle, &QTimer::timeout, this, [this] {
        // a drag still under way: not yet
        if (QGuiApplication::mouseButtons() != Qt::NoButton) {
            m_settle.start();
            return;
        }
        closePending();
    });
    watch();
}

AnnotationDocument *FrameHistory::document() const
{
    return SpectacleCore::instance() ? SpectacleCore::instance()->annotationDocument() : nullptr;
}

void FrameHistory::watch()
{
    auto selection = SelectionEditor::instance()->selection();
    m_last = selection->normalized();
    connect(selection, &Selection::rectChanged, this, &FrameHistory::frameChanged);
    if (auto doc = document()) {
        m_document = doc;
        m_marksDepth = doc->undoStackDepth();
        connect(doc, &AnnotationDocument::undoStackDepthChanged, this, &FrameHistory::marksChanged);
        connect(doc, &AnnotationDocument::redoStackDepthChanged, this, &FrameHistory::changed);
    }
}

void FrameHistory::frameChanged()
{
    const QRectF now = SelectionEditor::instance()->selection()->normalized();
    if (m_applying) {
        m_last = now;
        return;
    }
    if (now.isEmpty()) {
        // the frame let go (the overlay closed or started over): nothing to step back to
        reset();
        m_last = now;
        return;
    }
    if (CaptureTools::instance()->resizing()) {
        // a window resized to a picked size: the picture is taken again and the history starts over
        m_last = now;
        return;
    }
    if (!m_pending) {
        m_pending = true;
        m_pendingBefore = m_last;
    }
    m_last = now;
    m_settle.start();
    Q_EMIT changed();
}

void FrameHistory::closePending()
{
    m_settle.stop();
    if (!m_pending) {
        return;
    }
    m_pending = false;
    const QRectF after = m_last;
    // framing for the first time is no step: there was no frame to go back to
    if (m_pendingBefore.isEmpty() || after.isEmpty() || m_pendingBefore == after) {
        Q_EMIT changed();
        return;
    }
    auto doc = document();
    m_undo.append({m_pendingBefore, after, doc ? doc->undoStackDepth() : 0});
    // a new step: what could be redone is gone, the marks' too
    m_redo.clear();
    if (doc && doc->redoStackDepth() > 0) {
        m_applying = true;
        doc->clearRedo();
        m_applying = false;
    }
    Q_EMIT changed();
}

void FrameHistory::marksChanged()
{
    auto doc = document();
    const int depth = doc ? doc->undoStackDepth() : 0;
    // a new mark (not one put back by redo): the frame's steps to redo are gone, as the marks' are
    if (!m_applying && depth > m_marksDepth && doc && doc->redoStackDepth() == 0) {
        m_redo.clear();
    }
    m_marksDepth = depth;
    Q_EMIT changed();
}

void FrameHistory::apply(const QRectF &rect)
{
    m_applying = true;
    SelectionEditor::instance()->selection()->setRect(rect);
    m_applying = false;
    m_last = SelectionEditor::instance()->selection()->normalized();
}

bool FrameHistory::canUndo() const
{
    auto doc = document();
    return (m_pending && !m_pendingBefore.isEmpty() && m_pendingBefore != m_last) || !m_undo.isEmpty()
        || (doc && doc->undoStackDepth() > 0);
}

bool FrameHistory::canRedo() const
{
    auto doc = document();
    return !m_redo.isEmpty() || (doc && doc->redoStackDepth() > 0);
}

void FrameHistory::undo()
{
    closePending();
    auto doc = document();
    const int depth = doc ? doc->undoStackDepth() : 0;
    // the frame's latest step, unless a mark was made after it
    if (!m_undo.isEmpty() && depth <= m_undo.last().marks) {
        const Step step = m_undo.takeLast();
        apply(step.before);
        m_redo.append(step);
    } else if (doc && depth > 0) {
        m_applying = true;
        doc->undo();
        m_applying = false;
    }
    Q_EMIT changed();
}

void FrameHistory::redo()
{
    closePending();
    auto doc = document();
    const int depth = doc ? doc->undoStackDepth() : 0;
    const bool marksToRedo = doc && doc->redoStackDepth() > 0;
    // the frame's next step once the marks before it are back, else the next mark
    if (!m_redo.isEmpty() && (depth >= m_redo.last().marks || !marksToRedo)) {
        const Step step = m_redo.takeLast();
        apply(step.after);
        m_undo.append(step);
    } else if (marksToRedo) {
        m_applying = true;
        doc->redo();
        m_applying = false;
    }
    Q_EMIT changed();
}

void FrameHistory::reset()
{
    m_settle.stop();
    m_pending = false;
    m_undo.clear();
    m_redo.clear();
    m_last = SelectionEditor::instance()->selection()->normalized();
    Q_EMIT changed();
}

int FrameHistory::undoCount() const
{
    return m_undo.size();
}

int FrameHistory::redoCount() const
{
    return m_redo.size();
}

#include "moc_FrameHistory.cpp"
