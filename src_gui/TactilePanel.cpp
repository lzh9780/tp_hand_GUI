#include "TactilePanel.hpp"
#include "TactilePointCloud.hpp"
#include "TactileForcePlot.hpp"

#include <QGridLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QVariant>

namespace
{
/* Sensor keys as used by the telemetry map (HandStateMap module names). */
constexpr int kHeaderColumns = 2;

QString prettyName(const QString &raw)
{
    QString s = raw;
    s.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (!s.isEmpty())
        s[0] = s.at(0).toUpper();
    return s;
}
} // namespace

TactilePanel::TactilePanel(QWidget *parent)
    : QFrame(parent)
{
    setProperty("floatingPanel", QVariant(true));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    // The pair list can grow with the number of sensors, so it scrolls.
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *content = new QWidget;
    grid_ = new QGridLayout(content);
    grid_->setContentsMargins(2, 2, 2, 2);
    grid_->setHorizontalSpacing(8);
    grid_->setVerticalSpacing(10);
    grid_->setColumnStretch(0, 1);
    grid_->setColumnStretch(1, 1);

    scroll->setWidget(content);
    root->addWidget(scroll, 1);
}

TactilePanel::SensorRow *TactilePanel::rowFor(const QString &sensor)
{
    const std::string key = sensor.toStdString();
    auto it = rows_.find(key);
    if (it != rows_.end())
        return &it->second;

    const int row = static_cast<int>(rows_.size());

    // Column headers only once, above the first pair.
    if (row == 0)
    {
        auto *cloudHeader = new QLabel(tr("Point cloud"), this);
        auto *plotHeader = new QLabel(tr("Force"), this);
        for (auto *lbl : {cloudHeader, plotHeader})
        {
            lbl->setAlignment(Qt::AlignCenter);
            QFont f = lbl->font();
            f.setBold(true);
            lbl->setFont(f);
            lbl->setStyleSheet("color:#9aa3b0;");
        }
        grid_->addWidget(cloudHeader, 0, 0);
        grid_->addWidget(plotHeader, 0, 1);
    }

    const QString label = prettyName(sensor);
    const int gridRow = row + 1; // +1 for the header row

    auto *cloud = new TactilePointCloud(this);
    cloud->setTitle(label);
    cloud->setPressureRange(minPressure_, maxPressure_);

    auto *plot = new TactileForcePlot(this);
    // plot->setTitle(label);

    grid_->addWidget(cloud, gridRow, 0);
    grid_->addWidget(plot, gridRow, 1);

    SensorRow created{cloud, plot, QVector<QPointF>()};
    it = rows_.emplace(key, created).first;
    order_.append(sensor);
    return &it->second;
}

void TactilePanel::setSensors(const QVector<SensorLayout> &sensors)
{
    // Rebuild from scratch so a different config never leaves stale rows behind.
    for (auto &entry : rows_)
    {
        grid_->removeWidget(entry.second.cloud);
        grid_->removeWidget(entry.second.plot);
        delete entry.second.cloud;
        delete entry.second.plot;
    }
    rows_.clear();
    order_.clear();

    for (const SensorLayout &sensor : sensors)
    {
        SensorRow *row = rowFor(sensor.name);
        row->points = sensor.points;
        row->cloud->setPoints(sensor.points);
    }
}

void TactilePanel::setPressureRange(float minPressure, float maxPressure)
{
    minPressure_ = minPressure;
    maxPressure_ = maxPressure;
    for (auto &entry : rows_)
        entry.second.cloud->setPressureRange(minPressure, maxPressure);
}

void TactilePanel::resetHistory()
{
    for (auto &entry : rows_)
        entry.second.plot->clearHistory();
}

void TactilePanel::setHandPressureData(const QVector<SensorFrame> &frames)
{
    for (const SensorFrame &frame : frames)
    {
        auto it = rows_.find(frame.name.toStdString());
        if (it == rows_.end())
            continue;

        // The telemetry carries one scalar per sensor; every configured contact
        // point of that sensor shows the same reading.
        it->second.cloud->setPressure(frame.value);
        it->second.plot->appendSample(frame.value);
    }
}
