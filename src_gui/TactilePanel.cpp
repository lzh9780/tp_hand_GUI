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

    setSensors({QStringLiteral("THUMB"), QStringLiteral("INDEX_FINGER"),
                QStringLiteral("MIDDLE_FINGER"), QStringLiteral("RING_FINGER"),
                QStringLiteral("PINKY")});
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
    plot->setTitle(label);

    grid_->addWidget(cloud, gridRow, 0);
    grid_->addWidget(plot, gridRow, 1);

    SensorRow created{cloud, plot};
    it = rows_.emplace(key, created).first;
    order_.append(sensor);
    return &it->second;
}

void TactilePanel::setSensors(const QStringList &sensorNames)
{
    for (const QString &name : sensorNames)
        rowFor(name);
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

void TactilePanel::setHandPressureData(const HandPressureData &data)
{
    // Pair each sensor key with its matrix. Both halves always receive the same
    // frame so the cloud and the force trace stay in step.
    const QVector<QPair<QString, const PressureMatrix *>> frames = {
        {QStringLiteral("THUMB"), &data.thumb},
        {QStringLiteral("INDEX_FINGER"), &data.index},
        {QStringLiteral("MIDDLE_FINGER"), &data.middle},
        {QStringLiteral("RING_FINGER"), &data.ring},
        {QStringLiteral("PINKY"), &data.pinky},
    };

    for (const auto &frame : frames)
    {
        const PressureMatrix *matrix = frame.second;
        if (!matrix || matrix->rows <= 0 || matrix->cols <= 0)
            continue;
        if (matrix->values.size() != matrix->rows * matrix->cols)
            continue;

        SensorRow *row = rowFor(frame.first);

        TactilePointCloud::PressureMatrix cloudFrame;
        cloudFrame.rows = matrix->rows;
        cloudFrame.cols = matrix->cols;
        cloudFrame.values = matrix->values;
        row->cloud->setPressureMatrix(cloudFrame);

        // "Force" for a 1x1 sensor is the single reading; for a matrix it is the
        // sum of the cells, which is what a load cell behind the pad would see.
        double total = 0.0;
        for (float v : matrix->values)
            total += static_cast<double>(v);
        row->plot->appendSample(total);
    }
}
