// plotpanel.cpp
#include "PlotPanel.hpp"
#include "DiagramWidget.hpp"

#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>

PlotPanel::PlotPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);

    // top row: "Add diagram" button
    auto *topRow = new QHBoxLayout;
    m_addButton = new QPushButton(tr("+ Add diagram"), this);
    topRow->addWidget(m_addButton, 0);
    topRow->addStretch();
    root->addLayout(topRow);

    // vertical splitter to contain diagram modules
    m_splitter = new QSplitter(Qt::Vertical, this);
    root->addWidget(m_splitter, 1);

    connect(m_addButton, &QPushButton::clicked,
            this, &PlotPanel::onAddDiagram);
}

void PlotPanel::onAddDiagram()
{
    auto *diag = new DiagramWidget(m_splitter);
    diag->setMaxPoints(m_maxPoints);

    // add to splitter and track
    m_splitter->addWidget(diag);
    m_diagrams.append(diag);
}

void PlotPanel::updateFromHand(const HandStateMap &hand)
{
    for (DiagramWidget *d : m_diagrams)
        d->updateFromHand(hand);
}
