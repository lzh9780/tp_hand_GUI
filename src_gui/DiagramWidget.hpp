// DiagramWidget.hpp
#pragma once

#include <QWidget>
#include <QVector>
#include <QPointF>
#include <QString>
#include <deque>
#include "grip/data_def.hpp"

/**
 * DiagramWidget (GUI)
 * -------------------
 * Live diagram of the joint position, drawn in the same lightweight style as the
 * tactile plots (TactilePointCloud / TactileForcePlot): a plain QWidget with a
 * custom paintEvent — dark background, a soft grid, a filled curve and a small
 * title / readout in the corners. No QtCharts, no QtCharts view, no toolbar.
 *
 * It shows exactly one signal: the joint position in radians (UI design note #8).
 *
 * Interactions, all performed directly on the diagram:
 *
 *   wheel        — zoom in/out: how many samples the visible window holds.
 *   left/right   — drag to walk through the recorded history; dragging left
 *     drag         moves towards older samples.
 *
 * Freeze lives on the owning PlotPanel, which also renders the shared history.
 *
 * The plot carries a labelled X axis (time, in seconds) and a Y axis (joint
 * position, in radians); the value grid labels are drawn along the left edge.
 *
 * Everything else the old widget offered (signal picker, live/start modes,
 * frequency combo, CSV export, add/close buttons, scrollbars) has been removed.
 *
 * The sample history itself lives in the owning PlotPanel, which passes the
 * shared buffer to renderWindow(); the widget keeps no copy.
 */
class DiagramWidget : public QWidget
{
    Q_OBJECT
public:
    explicit DiagramWidget(QWidget *parent = nullptr);

    /* Joint this diagram plots (key as used in HandStateMap's JointGroup). */
    void setJointName(const QString &module, const QString &joint);
    QString moduleName() const { return moduleName_; }
    QString jointName() const { return jointName_; }

    /* Number of samples the visible window holds (wheel zoom target). */
    int windowSamples() const { return windowSamples_; }
    void setWindowSamples(int samples);

    /* Seconds between two samples, used to label the X axis. */
    void setSamplePeriod(double seconds) { samplePeriodSec_ = seconds; }
    /* How many samples have been dropped from the front of the shared buffer, so
       the X axis can be labelled with absolute time. */
    void setHistoryOffsetSamples(int dropped) { historyOffsetSamples_ = dropped; }

    /* Freeze / resume the live feed. While frozen the window stays put. */
    bool isFrozen() const { return frozen_; }
    void setFrozen(bool frozen);

    /* Offset of the window's right edge from the newest sample, in samples.
       0 = following live, larger = looking further back. */
    int historyOffset() const { return historyOffset_; }
    void setHistoryOffset(int offset);

    /* Oldest offset reachable given the current buffer length. */
    int maxHistoryOffset() const { return maxHistoryOffset_; }

    /* Scale (pixels per sample) applied by the zoom, for the panel's wheelEvent. */
    void zoomBy(double factor);

    /* Render the visible window of the shared history.
       `history` is referenced only for the duration of the call. */
    void renderWindow(const std::deque<HandStateMap> &history);

    /* Drop the cached samples so the plot renders empty until the next window
       arrives (used when the recorded history is cleared). */
    void clearWindow();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    /* Emitted whenever this diagram's own freeze / history / zoom changed, so
       the owning panel can rebuild just this one window. The sender is passed
       along because every diagram keeps its own view state. */
    void viewChanged(DiagramWidget *self);

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    /* Read this diagram's value out of one history sample. */
    bool valueAt(const HandStateMap &hand, float &outValue) const;

    /* Copy a reference to the shared history for the next paint. */
    void cacheWindow(const std::deque<HandStateMap> &history);

    QString moduleName_;
    QString jointName_;

    /* Default number of visible samples; leaving the frozen state restores it. */
    static constexpr int kDefaultWindowSamples = 250;

    /* View state. */
    int windowSamples_{kDefaultWindowSamples};
    int historyOffset_{0};   // samples back from the newest
    int maxHistoryOffset_{0};
    bool frozen_{false};

    /* Cached samples for the window currently on screen, oldest first. */
    std::deque<float> window_;

    /* Sample period, needed to label the X axis in seconds. */
    double samplePeriodSec_{0.02};
    /* Number of samples dropped from the front of the ring buffer so far; index
       of history[0] in the whole run. Keeps the X axis labels absolute. */
    int historyOffsetSamples_{0};
    /* Index (in the whole run) of the first sample currently plotted. */
    int windowStartSample_{0};
    /* Sample index (inside the shared buffer) the view is pinned to, so a frozen
       diagram can tell "no change" from "the user browsed elsewhere". */
    int frozenEnd_{-1};

    /* Drag-to-pan state. */
    bool dragging_{false};
    int dragStartX_{0};
    int dragStartOffset_{0};
};
