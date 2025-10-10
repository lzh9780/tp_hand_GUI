#pragma once
#include <QFrame>

class QWidget;

class TopPanel : public QFrame
{
    Q_OBJECT
public:
    explicit TopPanel(QWidget *parent);

signals:
    void deviceConnect();

private slots:
    void onConnect();
};