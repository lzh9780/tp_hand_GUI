// plotpanel.h
#pragma once

#include <QWidget>
#include <QVector>
#include "grip/can_defs.hpp"

class QPushButton;
class QSplitter;
class DiagramWidget;

using HandStateMap = std::unordered_map<std::string, grip::ModuleState>;

class PlotPanel : public QWidget
{
    Q_OBJECT
public:
    explicit PlotPanel(QWidget *parent = nullptr);

    void updateFromHand(const HandStateMap &hand);

private slots:
    void onAddDiagram();

private:
    QPushButton *m_addButton{nullptr};
    QSplitter *m_splitter{nullptr};
    QVector<DiagramWidget *> m_diagrams;
    int m_maxPoints{500};
};
