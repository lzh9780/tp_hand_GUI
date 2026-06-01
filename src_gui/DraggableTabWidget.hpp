#pragma once

#include <QTabWidget>
#include "DraggableTabBar.h"

class DraggableTabWidget : public QTabWidget
{
    Q_OBJECT
    public:
    explicit DraggableTabWidget(QWidget* parent = nullptr);
    
    protected:
        void dragEnterEvent(QDragEnterEvent *event) override;
        void dragMoveEvent(QDragMoveEvent *event) override;
        void dropEvent(QDropEvent *event) override;
};
