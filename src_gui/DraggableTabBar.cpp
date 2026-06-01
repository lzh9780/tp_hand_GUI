#include "DraggableTabBar.h"

#include <QMouseEvent>
#include <QDrag>
#include <QMimeData>
#include <QDataStream>
#include <QApplication>
#include <QTabWidget>

static const char *TAB_MIME_TYPE = "application/x-tab-move";

DraggableTabBar::DraggableTabBar(QWidget *parent)
    : QTabBar(parent)
{
    setAcceptDrops(true);
    setMovable(false); // we'll handle moving ourselves (also works with true)
}

void DraggableTabBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_dragStartPos = event->pos();
    }
    QTabBar::mousePressEvent(event);
}

void DraggableTabBar::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton))
    {
        QTabBar::mouseMoveEvent(event);
        return;
    }

    int distance = (event->pos() - m_dragStartPos).manhattanLength();
    if (distance < QApplication::startDragDistance())
    {
        QTabBar::mouseMoveEvent(event);
        return;
    }

    int index = tabAt(m_dragStartPos);
    if (index < 0)
    {
        QTabBar::mouseMoveEvent(event);
        return;
    }

    startDrag(index);
}

void DraggableTabBar::startDrag(int index)
{
    QTabWidget *srcTabs = qobject_cast<QTabWidget *>(parent());
    if (!srcTabs)
        return;

    auto *drag = new QDrag(this);
    auto *mime = new QMimeData;

    QByteArray ba;
    QDataStream ds(&ba, QIODevice::WriteOnly);
    ds << quintptr(srcTabs) << index; // pack source widget pointer + index

    mime->setData(TAB_MIME_TYPE, ba);
    drag->setMimeData(mime);
    drag->exec(Qt::MoveAction);
}

void DraggableTabBar::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasFormat(TAB_MIME_TYPE))
    {
        event->acceptProposedAction();
    }
    else
    {
        event->ignore();
    }
}

void DraggableTabBar::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasFormat(TAB_MIME_TYPE))
    {
        event->acceptProposedAction();
    }
    else
    {
        event->ignore();
    }
}

void DraggableTabBar::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()->hasFormat(TAB_MIME_TYPE))
    {
        event->ignore();
        return;
    }

    QByteArray ba = event->mimeData()->data(TAB_MIME_TYPE);
    QDataStream ds(&ba, QIODevice::ReadOnly);

    quintptr srcPtr;
    int srcIndex;
    ds >> srcPtr >> srcIndex;

    QTabWidget *srcTabs = reinterpret_cast<QTabWidget *>(srcPtr);
    QTabWidget *dstTabs = qobject_cast<QTabWidget *>(parent());
    if (!srcTabs || !dstTabs)
    {
        event->ignore();
        return;
    }

    if (srcIndex < 0 || srcIndex >= srcTabs->count())
    {
        event->ignore();
        return;
    }

    // Determine destination index from drop position
    int dstIndex = tabAt(event->pos());
    if (dstIndex < 0)
        dstIndex = dstTabs->count(); // drop at end

    // Take info from source BEFORE removing
    QWidget *page = srcTabs->widget(srcIndex);
    QString text = srcTabs->tabText(srcIndex);
    QIcon icon = srcTabs->tabIcon(srcIndex);
    QString tip = srcTabs->tabToolTip(srcIndex);

    // Remove from source
    srcTabs->removeTab(srcIndex);

    // If moving within same tab widget, adjust index after removal
    if (srcTabs == dstTabs && srcIndex < dstIndex)
        dstIndex--;

    // Insert into destination
    int newIndex = dstTabs->insertTab(dstIndex, page, icon, text);
    dstTabs->setTabToolTip(newIndex, tip);
    dstTabs->setCurrentIndex(newIndex);

    event->acceptProposedAction();
}
