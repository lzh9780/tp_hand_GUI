#include "DraggableTabWidget.hpp"
#include <QMouseEvent>
#include <QMimeData>
#include <QDataStream>

static const char *TAB_MIME_TYPE = "application/x-tab-move";

DraggableTabWidget::DraggableTabWidget(QWidget *parent)
    : QTabWidget(parent)
{
    setTabBar(new DraggableTabBar(this));  // allowed here (protected)
    setAcceptDrops(true);
}

void DraggableTabWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasFormat(TAB_MIME_TYPE)) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void DraggableTabWidget::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasFormat(TAB_MIME_TYPE)) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void DraggableTabWidget::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()->hasFormat(TAB_MIME_TYPE)) {
        event->ignore();
        return;
    }

    QByteArray ba = event->mimeData()->data(TAB_MIME_TYPE);
    QDataStream ds(&ba, QIODevice::ReadOnly);

    quintptr srcPtr;
    int srcIndex;
    ds >> srcPtr >> srcIndex;

    QTabWidget *srcTabs = reinterpret_cast<QTabWidget*>(srcPtr);
    QTabWidget *dstTabs = this;

    if (!srcTabs || srcIndex < 0 || srcIndex >= srcTabs->count()) {
        event->ignore();
        return;
    }

    QWidget *page = srcTabs->widget(srcIndex);
    QString text  = srcTabs->tabText(srcIndex);
    QIcon   icon  = srcTabs->tabIcon(srcIndex);
    QString tip   = srcTabs->tabToolTip(srcIndex);

    srcTabs->removeTab(srcIndex);

    // Here we simply append at the end (for empty widget, count() == 0)
    int dstIndex = dstTabs->count();

    // If moving within same widget and srcIndex < dstIndex, adjust (rare here)
    if (srcTabs == dstTabs && srcIndex < dstIndex)
        dstIndex--;

    int newIndex = dstTabs->insertTab(dstIndex, page, icon, text);
    dstTabs->setTabToolTip(newIndex, tip);
    dstTabs->setCurrentIndex(newIndex);

    event->acceptProposedAction();
}
