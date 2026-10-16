#pragma once

#include <QWidget>

class SideDock;

/**
 * MainWidget (GUI)
 * ----------------
 * Central widget of the main window, built as a stack of layers rather than a
 * geometry-driven layout:
 *
 *   +-------------------------------------------------------+
 *   | connection bar                                        |  <- fixed top row
 *   +-------------------------------------------------------+
 *   |   ~~~~~~~~~~~~ 3D model (background layer) ~~~~~~~~~~ |
 *   |  +---------+                              +---------+ |
 *   |  |  LEFT   |    panels float above it     |  RIGHT  | |
 *   |  |  dock   |                              |  dock   | |
 *   |  +---------+                              +---------+ |
 *   +-------------------------------------------------------+
 *
 * The 3D widget is a plain child that always fills the whole area below the top
 * bar. The docks are siblings positioned *on top of* it and raised, so showing,
 * hiding or resizing a dock never changes the 3D widget's geometry — the model
 * keeps rendering at a constant size and the panels simply cover more or less
 * of it.
 *
 * Only the top bar takes part in a layout (it is a separate row, not part of the
 * 3D area); everything else is placed by hand in recomputeGeometry().
 */
class MainWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MainWidget(QWidget *parent = nullptr);

    /* 3D view: bottom layer, always fills the area under the top bar.
       Not owned by this widget. */
    void setBackgroundWidget(QWidget *background);

    /* Fixed strip at the very top. Not owned. */
    void setTopBar(QWidget *topBar);

    /* Panel columns floating above the 3D view. Not owned. Their collapsed /
       expanded state only affects their own width, never the 3D geometry. */
    void setLeftDock(SideDock *dock);
    void setRightDock(SideDock *dock);

    /* Gap between the 3D area border and the floating docks. */
    void setOverlayMargins(int px);

    /* Re-place the layers. Call after a dock changed its own width (e.g. it was
       collapsed), since none of the docks take part in a layout. */
    void relayout() { recomputeGeometry(); }

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void recomputeGeometry();

    QWidget *background_{nullptr};
    QWidget *topBar_{nullptr};
    SideDock *leftDock_{nullptr};
    SideDock *rightDock_{nullptr};

    int margin_{0};
};
