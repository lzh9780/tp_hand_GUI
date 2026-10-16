#pragma once

#include <QWidget>
#include <QVector>
#include <QPointF>
#include <QString>
#include <deque>

/**
 * TactileForcePlot (GUI)
 * ----------------------
 * Right half of one tactile sensor pair: the total (or peak) force reported by
 * the sensor over time.
 *
 * The sensor frame only carries the current pressure matrix, so this widget
 * accumulates the derived force itself and keeps a short rolling history, which
 * is what makes contact onsets / releases readable.
 */
class TactileForcePlot : public QWidget
{
    Q_OBJECT
public:
    explicit TactileForcePlot(QWidget *parent = nullptr);

    void setTitle(const QString &title);

    /* Append one sample. `force` is expected to be the summed cell pressure. */
    void appendSample(double force);

    /* Forget the history (used right after a reconnect). */
    void clearHistory();

    /* Maximum number of samples kept / drawn. */
    void setHistoryLimit(int samples);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString title_;
    std::deque<double> history_;
    int historyLimit_{300};

    double peak_{1.0};
};
