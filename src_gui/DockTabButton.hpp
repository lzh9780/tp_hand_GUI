#pragma once

#include <QAbstractButton>
#include <QString>

/**
 * DockTabButton (GUI)
 * -------------------
 * One tab of a SideDock. It is a QAbstractButton that paints itself through
 * QStyle using QStyleOptionTab, so the result looks exactly like a real Qt tab
 * (same shape, colours, hover and selected states as the active widget style)
 * while still allowing the label to be rotated 90°.
 *
 * The button occupies a short, wide rectangle that is painted rotated:
 *
 *   on screen                 internal layout (before rotation)
 *   +--------+                 +--------------+
 *   |   P    |                 |    Plots     |  <- normal horizontal tab
 *   |   l    |   <- rotated    +--------------+
 *   |   o    |      painting
 *   |   t    |      of the
 *   |   s    |      horizontal tab
 *   +--------+
 *
 * QStyleOptionTab cannot carry a rotated label, so the trick is: build the
 * option for the *rotated* geometry, draw the frame/background with QStyle, then
 * draw the text ourselves inside the rotated coordinate system.
 */
class DockTabButton : public QAbstractButton
{
    Q_OBJECT
public:
    explicit DockTabButton(const QString &text, bool rotateClockwise, QWidget *parent = nullptr);

    /* true = text reads top-to-bottom (left dock),
       false = text reads bottom-to-top (right dock). */
    void setRotateClockwise(bool clockwise);

    /* Position of this tab inside the tab bar, used by QStyle for the tab shape
       (first / last / middle tab look different). */
    void setTabPosition(int index, int total);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    bool event(QEvent *event) override;
    void enterEvent(QEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QSize innerSize() const;

    bool clockwise_;
    bool hovered_{false};
    int tabIndex_{0};
    int tabCount_{1};
};
