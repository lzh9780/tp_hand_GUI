// PlotPanel.hpp
#pragma once

#include <QFrame>
#include <QVector>
#include <QPushButton>
#include <QSet>
#include <deque>

#include "DiagramWidget.hpp"
#include "grip/data_def.hpp"

class QGridLayout;
class QScrollArea;
class QScrollBar;

/**
 * PlotPanel
 * ---------
 * Joint-position diagrams, laid out like the tactile panel: a scrolling grid of
 * small self-painted widgets (UI design note #8). One diagram is created per
 * actuated joint the first time the hand reports it, so the list matches the
 * robot instead of being assembled by hand.
 *
 * Only three controls are kept, all at the top of the panel:
 *
 *   Freeze     — stop following live data; the frozen window stays on screen so
 *                a transient can be read while acquisition continues.
 *   Reset zoom — bring every diagram back to the default window width.
 *   Clear      — discard the recorded history and start collecting from zero.
 *
 * Browsing and scaling are done directly on a diagram:
 *
 *   wheel        — zoom in/out (how many samples the window holds)
 *   left/right   — drag to walk through the recorded history
 *     drag
 *
 * The panel owns the shared sample history; the diagrams are pure views onto it.
 *
 * Removed on purpose: signal picker, frequency combo, Start/live mode buttons,
 * CSV export, add/close diagram buttons, per-diagram scrollbars, and the history
 * / zoom slider widgets (now gestures on the diagrams themselves).
 */
class PlotPanel : public QFrame
{
    Q_OBJECT
public:
    explicit PlotPanel(QWidget *parent = nullptr);

    /* Called by MainWindow's timer with the newest hand state. */
    void feedData(const HandStateMap &hand);

    /* Create one diagram per joint found in `hand`; existing ones are kept. */
    void syncDiagramsWithHand(const HandStateMap &hand);
    /* Drop every diagram (used when a new model is loaded). */
    void clearDiagrams();

    /* Forget the recorded history (on disconnect). */
    void resetHistory();

protected:
    /* Keeps the grid host as wide as the viewport while leaving its height to the
       diagrams, so the scroll area scrolls vertically instead of squeezing. */
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onFreezeToggled(bool frozen);
    /* Bring every diagram back to the default window width / live edge. */
    void onResetZoom();
    /* Discard the recorded history and start collecting from zero. */
    void onClearHistory();
    /* One diagram changed its own view (wheel zoom / drag): rebuild just it. */
    void onDiagramViewChanged(DiagramWidget *diagram);

private:
    DiagramWidget *createDiagram(const QString &module, const QString &joint);
    void buildUi();
    /* Size the grid host to the viewport width / content height (see the .cpp). */
    void syncGridHostSize();

    /* ---- view state ---- */
    /* Freeze and the default window width are panel-wide settings: the buttons
       act on everything at once. Zoom and the history position live on each
       diagram, so those two are only kept here as the values a *newly created*
       diagram starts from. */
    bool frozen_{false};
    int windowSamples_{250}; // default; "Reset zoom" restores this
    int historyOffset_{0};

    /* ---- widgets ---- */
    QPushButton *freezeBtn_{nullptr};
    QPushButton *resetZoomBtn_{nullptr};
    QPushButton *clearBtn_{nullptr};

    QScrollArea *scroll_{nullptr};
    QWidget *gridHost_{nullptr};
    QGridLayout *grid_{nullptr};

    QVector<DiagramWidget *> diagrams_;
    /* "module/joint" keys already turned into a diagram. */
    QSet<QString> knownJoints_;

    /* Shared history. */
    std::deque<HandStateMap> history_;
    int historyLimit_{5000};
    int totalSamples_{0};
    /* Samples dropped from the front of the ring buffer so far, so diagrams can
       label the X axis with absolute time. */
    int droppedSamples_{0};
    /* Seconds between two samples (the feed runs at the plot timer's rate). */
    double samplePeriodSec_{0.02};
};
