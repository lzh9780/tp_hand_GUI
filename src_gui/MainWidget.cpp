#include "MainWidget.hpp"
#include "SideDock.hpp"

#include <QResizeEvent>
#include <QWidget>
#include <QDebug>

MainWidget::MainWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(900, 560);

    // No layout: the 3D view must keep a constant size while the docks float on
    // top of it, which a layout could not express (it would shrink the 3D view
    // whenever a dock column appears).
    setAutoFillBackground(false);
}

void MainWidget::setBackgroundWidget(QWidget *background)
{
    background_ = background;
    if (background_)
    {
        background_->setParent(this);
        background_->lower(); // bottom-most layer
    }
    recomputeGeometry();
}

void MainWidget::setTopBar(QWidget *topBar)
{
    topBar_ = topBar;
    if (topBar_)
        topBar_->setParent(this);
    recomputeGeometry();
}

void MainWidget::setLeftDock(SideDock *dock)
{
    leftDock_ = dock;
    if (leftDock_)
        leftDock_->setParent(this);
    recomputeGeometry();
}

void MainWidget::setRightDock(SideDock *dock)
{
    rightDock_ = dock;
    if (rightDock_)
        rightDock_->setParent(this);
    recomputeGeometry();
}

void MainWidget::setOverlayMargins(int px)
{
    margin_ = qMax(0, px);
    recomputeGeometry();
}

void MainWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    recomputeGeometry();
}

void MainWidget::recomputeGeometry()
{
    const int w = width();
    const int h = height();

    /* ---- 1. top bar: the only widget outside the 3D area ---- */
    int top = 0;
    if (topBar_)
    {
        const int barH = topBar_->sizeHint().height();
        topBar_->setGeometry(0, 0, w, barH);
        topBar_->raise();
        top = barH;
    }

    /* ---- 2. 3D view: always the full area below the top bar ----
       Its geometry depends only on the widget size, never on the docks, so
       hiding or showing a panel cannot resize or re-centre the model. */
    if (background_)
    {
        background_->setGeometry(0, top, w, h - top);
        background_->lower();
    }

    /* ---- 3. docks: floated on top of the 3D view ---- */
    const int areaTop = top + margin_;
    const int areaBottom = h - margin_;
    const int areaH = areaBottom - areaTop;

    if (leftDock_)
    {
        // The left column is anchored to the left window edge: collapsing it
        // shrinks it towards that edge, so its tab strip (the inner part) slides
        // left and stays right next to the 3D view. x is therefore unchanged.
        const int dockW = leftDock_->currentWidth();
        leftDock_->setGeometry(margin_, areaTop, dockW, qMax(0, areaH));
        leftDock_->raise();
    }

    if (rightDock_)
    {
        // The right column is anchored to the right window edge, mirroring the
        // left one: collapsing just makes it narrower while its outer edge stays
        // put, so the tab strip never drifts into the middle of the window.
        //
        //   expanded:  [ content | strip ]   x = w - m - expandedWidth
        //   collapsed: [ strip ]             x = w - m - stripWidth
        qDebug() << "screen width: " << w;
        const int dockW = rightDock_->currentWidth();
        const int x = w - margin_ - dockW;
        qDebug() << "right panel width: " << dockW;
        qDebug() << "right panel pos: " << x;

        rightDock_->setGeometry(x, areaTop, dockW, qMax(0, areaH));
        rightDock_->raise();
    }
}
