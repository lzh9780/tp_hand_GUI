// ledindicator.h
#ifndef LEDINDICATOR_H
#define LEDINDICATOR_H

#include <QWidget>
#include <QColor>

class LedIndicator : public QWidget
{
    Q_OBJECT
public:
    explicit LedIndicator(QWidget *parent = nullptr);

    void setGreen(bool on);
    void setColor(const QColor &color);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QColor m_color;
};

#endif // LEDINDICATOR_H