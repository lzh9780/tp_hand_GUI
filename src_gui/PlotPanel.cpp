// PlotPanel.cpp
#include "PlotPanel.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVariant>
#include <QSignalBlocker>
#include <QScrollBar>
#include <QEvent>
#include <algorithm>
#include <cmath>

namespace
{
/* Default window width, in samples. Used by the initial view, by a newly created
   diagram and by the "Reset zoom" button. */
constexpr int kDefaultWindow = 250;

/* Fixed height of one diagram inside the scroll area. */
constexpr int kDiagramHeight = 240;
} // namespace

PlotPanel::PlotPanel(QWidget *parent)
    : QFrame(parent)
{
    // Same translucent panel look as the other dock pages.
    setProperty("floatingPanel", QVariant(true));
    buildUi();
}

void PlotPanel::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    /* ---------------- controls: freeze / reset zoom / clear ----------------
       Scaling is done with the wheel and history is browsed by dragging directly
       on a diagram, so no slider widgets are needed here. */
    auto *controls = new QWidget(this);
    auto *cLyt = new QHBoxLayout(controls);
    cLyt->setContentsMargins(0, 0, 0, 0);
    cLyt->setSpacing(6);

    freezeBtn_ = new QPushButton(tr("Freeze"), controls);
    freezeBtn_->setCheckable(true);
    freezeBtn_->setFixedHeight(24);
    freezeBtn_->setToolTip(tr("Stop following live data so the current window "
                              "stays on screen"));
    cLyt->addWidget(freezeBtn_);

    resetZoomBtn_ = new QPushButton(tr("Reset zoom"), controls);
    resetZoomBtn_->setFixedHeight(24);
    resetZoomBtn_->setToolTip(tr("Restore every diagram to the default window "
                                 "width and back to the live edge"));
    cLyt->addWidget(resetZoomBtn_);

    clearBtn_ = new QPushButton(tr("Clear"), controls);
    clearBtn_->setFixedHeight(24);
    clearBtn_->setToolTip(tr("Discard the recorded history and start collecting "
                             "from zero"));
    cLyt->addWidget(clearBtn_);

    cLyt->addStretch(1);
    root->addWidget(controls);

    /* ---------------- diagram grid inside a scroll area ----------------
       The scroll area holds the plots: each diagram keeps a fixed height, so a
       long list scrolls instead of squeezing every plot into the visible space.
       `widgetResizable` is left off for the same reason — with it on, the host
       would shrink to the viewport and the plots would be compressed rather than
       scrolled. */
    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(false);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    gridHost_ = new QWidget;
    grid_ = new QGridLayout(gridHost_);
    grid_->setContentsMargins(2, 2, 2, 2);
    grid_->setHorizontalSpacing(8);
    grid_->setVerticalSpacing(8);
    // Single column: the panel lives in a narrow dock column, so side-by-side
    // diagrams would be too small to read.
    grid_->setColumnStretch(0, 1);

    scroll_->setWidget(gridHost_);
    root->addWidget(scroll_, 1);

    // Keep the plots as wide as the viewport: the horizontal scrollbar is off, so
    // the host must track the available width while keeping its own height.
    scroll_->viewport()->installEventFilter(this);
    connect(scroll_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]()
            {
                // Nothing to do beyond letting the viewport scroll, but keeping the
                // connection documents that the scrollbar is the intended way to
                // reach the plots that are out of view.
            });

    /* ---------------- wiring ---------------- */
    connect(freezeBtn_, &QPushButton::toggled, this, &PlotPanel::onFreezeToggled);
    connect(resetZoomBtn_, &QPushButton::clicked, this, &PlotPanel::onResetZoom);
    connect(clearBtn_, &QPushButton::clicked, this, &PlotPanel::onClearHistory);

    setMinimumWidth(240);
}

bool PlotPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (scroll_ && watched == scroll_->viewport() && event->type() == QEvent::Resize)
        syncGridHostSize();
    return QFrame::eventFilter(watched, event);
}

/*
 * Size the grid host to the viewport width and its own content height.
 *
 * With widgetResizable disabled the scroll area never resizes its widget, so the
 * host would keep its initial (tiny) size and there would be nothing to scroll.
 * Width comes from the viewport (the horizontal scrollbar is off), height from
 * the layout's preferred size, which is the sum of the fixed-height diagrams.
 */
void PlotPanel::syncGridHostSize()
{
    if (!scroll_ || !gridHost_ || !grid_)
        return;

    const int w = std::max(1, scroll_->viewport()->width());

    // The height is derived from the number of diagrams rather than from the
    // layout's hint: at the moment a diagram is created the layout has not run
    // yet, and its hint would still report the old (near-empty) height, which
    // left the host a few pixels tall and the view unscrollable.
    const int rows = grid_->rowCount();
    const int spacing = grid_->verticalSpacing();
    const QMargins m = grid_->contentsMargins();
    const int h = std::max(1, rows * kDiagramHeight + std::max(0, rows - 1) * spacing +
                                  m.top() + m.bottom());

    // Always apply both, unconditionally: the host must track the viewport width
    // even when its height did not change, and the scroll area reads the widget's
    // size to decide whether to show a scrollbar.
    gridHost_->setMinimumSize(w, h);
    gridHost_->resize(w, h);
}

DiagramWidget *PlotPanel::createDiagram(const QString &module, const QString &joint)
{
    auto *diag = new DiagramWidget(gridHost_);
    diag->setJointName(module, joint);
    // Fixed height so a long list scrolls inside the scroll area instead of
    // squeezing every plot; the width follows the viewport (see eventFilter).
    diag->setFixedHeight(kDiagramHeight);

    // A new diagram starts at the default zoom and at the live edge; zoom and
    // history are per-diagram from then on.
    diag->setWindowSamples(kDefaultWindow);
    diag->setFrozen(frozen_);
    diag->setHistoryOffset(0);
    // Sample period and buffer origin, so the X axis can be labelled with the
    // real time each sample was taken at.
    diag->setSamplePeriod(samplePeriodSec_);
    diag->setHistoryOffsetSamples(droppedSamples_);

    const int row = grid_->rowCount();
    grid_->addWidget(diag, row, 0);

    diagrams_.append(diag);
    knownJoints_.insert(module + "/" + joint);

    // Each diagram keeps its own zoom / history position: a change only rebuilds
    // the diagram that reported it.
    connect(diag, &DiagramWidget::viewChanged, this, &PlotPanel::onDiagramViewChanged);

    // The host grew by one fixed-height row: refresh the scrollable size.
    syncGridHostSize();
    return diag;
}

void PlotPanel::syncDiagramsWithHand(const HandStateMap &hand)
{
    for (const auto &[moduleName, group] : hand)
    {
        const QString module = QString::fromStdString(moduleName);
        for (const auto &[jointName, joint] : group.joints)
        {
            const QString joint_ = QString::fromStdString(jointName);
            if (knownJoints_.contains(module + "/" + joint_))
                continue;
            createDiagram(module, joint_);
        }
    }
}

void PlotPanel::clearDiagrams()
{
    for (auto *diag : diagrams_)
        diag->deleteLater();
    diagrams_.clear();
    knownJoints_.clear();
}

void PlotPanel::resetHistory()
{
    history_.clear();
    totalSamples_ = 0;
    droppedSamples_ = 0;
    historyOffset_ = 0;

    // A diagram may be zoomed in or parked far back in the history; both
    // positions refer to samples that no longer exist, so the views are returned
    // to the default window at the live edge. Without this a frozen diagram would
    // keep showing the (now stale) curve it had cached.
    for (auto *d : diagrams_)
    {
        const QSignalBlocker block(d);
        d->setWindowSamples(kDefaultWindow);
        d->setHistoryOffset(0);
        d->setHistoryOffsetSamples(0);
        // Drop the cached samples last, so the plot is blank until the next
        // window is rendered. renderWindow() cannot do this: with an empty
        // history it returns early and would leave the old curve on screen.
        d->clearWindow();
    }
}

void PlotPanel::onClearHistory()
{
    // Discard everything recorded so far; the buffers start filling again from
    // the next sample.
    resetHistory();

    // Clearing also leaves the frozen state: there is no captured past left to
    // look at, and staying frozen would keep the plots empty while new samples
    // arrive (a frozen diagram does not follow the feed). Returning to live is
    // what makes the panel usable again straight away.
    if (freezeBtn_ && freezeBtn_->isChecked())
        freezeBtn_->setChecked(false); // triggers onFreezeToggled(false)
    else
        frozen_ = false;
}

void PlotPanel::feedData(const HandStateMap &hand)
{
    // A diagram per joint, then the new sample for the shared buffer.
    syncDiagramsWithHand(hand);

    history_.push_back(hand);
    while (static_cast<int>(history_.size()) > historyLimit_)
    {
        history_.pop_front();
        ++droppedSamples_; // keep the absolute sample index accurate
    }
    ++totalSamples_;

    // Keep the absolute buffer origin up to date so the X axis labels stay
    // correct once the ring buffer starts dropping old samples.
    for (auto *d : diagrams_)
        d->setHistoryOffsetSamples(droppedSamples_);

    // Nothing more to do while frozen: the diagrams keep the window they show.
    if (frozen_)
        return;

    for (auto *d : diagrams_)
        d->renderWindow(history_);
}

/* ------------------------------------------------------------------ *
 *  Controls
 * ------------------------------------------------------------------ */

void PlotPanel::onFreezeToggled(bool frozen)
{
    frozen_ = frozen;
    freezeBtn_->setText(frozen ? tr("Resume") : tr("Freeze"));

    // setFrozen() itself drops the zoom / history position when leaving the
    // frozen state, so a fresh live view always starts from the default window at
    // the newest sample.
    for (auto *d : diagrams_)
    {
        const QSignalBlocker block(d);
        d->setFrozen(frozen);
    }

    if (!frozen)
        historyOffset_ = 0; // starting value for diagrams created later

    // Render so the frozen window (or the live edge after resuming) is captured
    // straight away instead of waiting for the next sample.
    for (auto *d : diagrams_)
        d->renderWindow(history_);
}

void PlotPanel::onResetZoom()
{
    // Default window width and back to the live edge, for every diagram. The
    // zoom is per-diagram, so each one is reset explicitly.
    windowSamples_ = kDefaultWindow;
    historyOffset_ = 0;

    for (auto *d : diagrams_)
    {
        const QSignalBlocker block(d);
        d->setWindowSamples(kDefaultWindow);
        d->setHistoryOffset(0);
    }

    for (auto *d : diagrams_)
        d->renderWindow(history_);
}

void PlotPanel::onDiagramViewChanged(DiagramWidget *diagram)
{
    // Each diagram has its own zoom / history position, so a change only has to
    // be applied to the diagram that reported it — no mirroring, and therefore no
    // signal echo to guard against either.
    if (!diagram)
        return;

    // Rebuild that diagram's window right away. This is needed in every case,
    // also while live: dragging away from the live edge parks the view in the
    // past without freezing, so the curve has to follow the new offset
    // immediately instead of waiting for the next sample tick.
    diagram->renderWindow(history_);
}
