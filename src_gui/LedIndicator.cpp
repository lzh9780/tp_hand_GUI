// ledindicator.cpp
#include "LedIndicator.hpp"
#include <QPainter>
#include <QRadialGradient>

LedIndicator::LedIndicator(QWidget *parent)
    : QWidget(parent), m_color(Qt::red)
{
    setFixedSize(30, 30);
    // Make background transparent
    setAttribute(Qt::WA_TranslucentBackground);
    // Ensure no background painting
    setAutoFillBackground(false);
}

void LedIndicator::setGreen(bool on)
{
    m_color = on ? Qt::green : Qt::red;
    update();
}

void LedIndicator::setColor(const QColor &color)
{
    m_color = color;
    update();
}

QSize LedIndicator::sizeHint() const
{
    return QSize(30, 30);
}

void LedIndicator::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Draw only the glowing circle – no outer ring
    QRectF circleRect = rect().adjusted(2, 2, -2, -2);

    // Radial gradient: center bright, edge transparent
    QRadialGradient gradient(circleRect.center(), circleRect.width() / 2.0);
    gradient.setColorAt(0.0, m_color.lighter(150));
    gradient.setColorAt(0.4, m_color);
    gradient.setColorAt(1.0, QColor(0, 0, 0, 0)); // fully transparent at edge

    painter.setBrush(gradient);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(circleRect);
}