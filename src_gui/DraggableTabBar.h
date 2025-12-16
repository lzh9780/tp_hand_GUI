#pragma once

#include <QTabBar>
#include <QPoint>

class DraggableTabBar : public QTabBar
{
    Q_OBJECT
public:
    explicit DraggableTabBar(QWidget *parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    QPoint m_dragStartPos;

    void startDrag(int index);
};
