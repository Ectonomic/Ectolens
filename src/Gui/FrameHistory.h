/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QRectF>
#include <QTimer>

class AnnotationDocument;

/**
 * Ectolens: the overlay's Undo and Redo, for the frame as well as the marks, so
 * undo and redo also handle frame resizes and moving the frame.
 *
 * The marks keep their own history in the AnnotationDocument; this keeps the frame's, and
 * steps through both in the order things happened. Each frame step remembers how many mark
 * steps came before it (the document's undo depth then), which is all it takes to put the two
 * in one line: Undo takes back the frame step when no mark came after it, else the latest mark;
 * Redo puts back the frame step once the marks before it are back, else the next mark.
 *
 * A drag, a handle pulled or a run of arrow keys is one step: a change to the frame opens a
 * step, and it closes once nothing has moved for a moment and no button is held (or at once,
 * when Undo is pressed). Framing for the first time is not a step, nor is letting the frame
 * go. A size picked for a window resizes the window and takes the picture again, which clears
 * the marks: the history starts over there (reset()).
 */
class FrameHistory : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed FINAL)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed FINAL)

public:
    static FrameHistory *instance();
    static FrameHistory *create(QQmlEngine *engine, QJSEngine *)
    {
        auto inst = instance();
        Q_ASSERT(inst->thread() == engine->thread());
        QJSEngine::setObjectOwnership(inst, QJSEngine::CppOwnership);
        return inst;
    }

    bool canUndo() const;
    bool canRedo() const;

    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    /// Forget the frame's steps (a new capture, or the picture taken again).
    void reset();

    /// how many frame steps can be undone and redone (for the test script)
    int undoCount() const;
    int redoCount() const;

Q_SIGNALS:
    void changed();

private:
    explicit FrameHistory(QObject *parent = nullptr);
    struct Step {
        QRectF before;
        QRectF after;
        /// the marks' undo depth when this step was made
        int marks = 0;
    };
    AnnotationDocument *document() const;
    void watch();
    void frameChanged();
    void marksChanged();
    void closePending();
    void apply(const QRectF &rect);

    QList<Step> m_undo;
    QList<Step> m_redo;
    bool m_pending = false;
    QRectF m_pendingBefore;
    QRectF m_last;
    bool m_applying = false;
    int m_marksDepth = 0;
    QTimer m_settle;
    QPointer<AnnotationDocument> m_document;
};
