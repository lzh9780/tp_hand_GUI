#include "DockTabButton.hpp"

#include <QPainter>
#include <QPaintEvent>
#include <QStyleOptionTab>
#include <QStyle>
#include <QTabBar>
#include <QFontMetrics>
#include <QColor>

namespace
{
/* Padding inside the tab, along the text direction and across it. Matches the
   QTabBar padding so the tab keeps a native look. */
constexpr int kTextPad = 14;
// Extra space across the tab: the style's tab shape (rounded top + border)
// eats into the rect, so the thickness must exceed the font box by more than
// the frame insets or the glyphs get clipped.
constexpr int kCrossPad = 12;
} // namespace

DockTabButton::DockTabButton(const QString &text, bool rotateClockwise, QWidget *parent)
    : QAbstractButton(parent), clockwise_(rotateClockwise)
{
    // Named so the style sheet can keep the global "QWidget { background-color }"
    // rule away from the tab: the shape is drawn by QStyle in paintEvent().
    setObjectName("dockTab");
    setText(text);
    setCheckable(true);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
    // The tab paints its own shape; the global "QWidget { background-color: ... }"
    // rule must not fill the button behind it, otherwise the shape (and the
    // rotated label) would be covered by an opaque rectangle.
    setAttribute(Qt::WA_StyledBackground, false);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setAutoFillBackground(false);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    // Explicit colours instead of the palette inherited from the parent panel,
    // which is translucent and would make the text unreadable.
    QPalette pal = palette();
    pal.setColor(QPalette::WindowText, QColor(214, 219, 226));
    pal.setColor(QPalette::Text, QColor(214, 219, 226));
    pal.setColor(QPalette::ButtonText, QColor(214, 219, 226));
    setPalette(pal);
}

void DockTabButton::setRotateClockwise(bool clockwise)
{
    if (clockwise_ == clockwise)
        return;
    clockwise_ = clockwise;
    updateGeometry();
    update();
}

void DockTabButton::setTabPosition(int index, int total)
{
    if (tabIndex_ == index && tabCount_ == total)
        return;
    tabIndex_ = index;
    tabCount_ = total;
    update();
}

QSize DockTabButton::innerSize() const
{
    const QFontMetrics fm(font());
    // The label runs along the screen's Y axis, the font height along X. The
    // tab is sized generously so the style's own insets never clip the text.
    const int len = fm.horizontalAdvance(text()) + 2 * kTextPad;
    const int thick = fm.height() + 2 * kCrossPad;
    return QSize(thick, len);
}

QSize DockTabButton::sizeHint() const
{
    return innerSize();
}

QSize DockTabButton::minimumSizeHint() const
{
    // A real tab never shrinks below its label.
    return innerSize();
}

bool DockTabButton::event(QEvent *event)
{
    // The tab is painted rotated, so its visual size follows the font.
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
        updateGeometry();
    return QAbstractButton::event(event);
}

void DockTabButton::enterEvent(QEvent *event)
{
    hovered_ = true;
    update();
    QAbstractButton::enterEvent(event);
}

void DockTabButton::leaveEvent(QEvent *event)
{
    hovered_ = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void DockTabButton::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    const QSize inner = innerSize();
    const QRect screenRect = rect();

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    // The button owns its whole cell and paints everything itself, so the
    // translucent panel background behind it shows through the corners.
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(screenRect, Qt::transparent);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);

    /* ---- 1. tab frame: ask QStyle for the real tab shape ----
       The option is built as if the tab were a normal horizontal tab, then the
       painter is rotated 90° about the cell centre. Working in the rotated
       frame, a horizontal tab of size `inner` must be drawn into
       QRect(-inner.height()/2, -inner.width()/2, inner.height(), inner.width())
       so that after the rotation it covers the cell exactly. */
    p.save();
    p.translate(screenRect.center());
    p.rotate(clockwise_ ? 90.0 : -90.0);

    // Rect of the horizontal tab in the rotated coordinate system.
    const QRect tabRect(-inner.height() / 2, -inner.width() / 2,
                        inner.height(), inner.width());

    QStyleOptionTab opt;
    opt.initFrom(this);
    // The style works on the horizontal tab rect expressed in the rotated frame.
    opt.rect = tabRect;
    // Tabs run down the strip, i.e. along the screen's Y axis.
    opt.shape = QTabBar::RoundedNorth;
    opt.text = QString(); // the label is drawn separately below
    // The dark tab colours must come from explicit palette roles: the parent
    // panel is translucent, so relying on the inherited palette made the style
    // fall back to light defaults and paint a near-white tab.
    opt.palette.setColor(QPalette::Window, QColor(46, 50, 58));
    opt.palette.setColor(QPalette::Button, QColor(46, 50, 58));
    opt.palette.setColor(QPalette::Base, QColor(38, 42, 48));
    opt.palette.setColor(QPalette::WindowText, QColor(214, 219, 226));
    opt.palette.setColor(QPalette::ButtonText, QColor(214, 219, 226));
    opt.position = QStyleOptionTab::OnlyOneTab;
    if (tabCount_ > 1)
    {
        if (tabIndex_ == 0)
            opt.position = QStyleOptionTab::Beginning;
        else if (tabIndex_ == tabCount_ - 1)
            opt.position = QStyleOptionTab::End;
        else
            opt.position = QStyleOptionTab::Middle;
    }
    opt.selectedPosition = QStyleOptionTab::NotAdjacent;
    opt.features = QStyleOptionTab::None;

    // Build on the state set up by initFrom() and only add the tab-specific
    // flags, so the widget palette / active-window shading are preserved.
    opt.state &= ~(QStyle::State_Selected | QStyle::State_Sunken |
                   QStyle::State_MouseOver | QStyle::State_Raised);
    if (isChecked())
        opt.state |= QStyle::State_Selected;
    if (hovered_ || underMouse())
        opt.state |= QStyle::State_MouseOver;
    if (isDown())
        opt.state |= QStyle::State_Sunken;

    style()->drawControl(QStyle::CE_TabBarTabShape, &opt, &p, this);

    /* ---- 2. label ----
       Drawn by hand rather than through CE_TabBarTabLabel: the style would use
       the (translucent) inherited palette and the text could end up invisible.
       The painter is already rotated, so an ordinary horizontal drawText lands
       as vertical text on screen.

       The text rect is inset by the style's own tab spacing so the glyphs stay
       inside the drawn shape instead of being clipped by its edges. */
    QFont f = font();
    f.setBold(isChecked());
    p.setFont(f);

    const int hSpace = style()->pixelMetric(QStyle::PM_TabBarTabHSpace, &opt, this) / 2;
    const int vSpace = style()->pixelMetric(QStyle::PM_TabBarTabVSpace, &opt, this) / 2;
    const QRect textRect = opt.rect.adjusted(hSpace, vSpace, -hSpace, -vSpace);

    // Contrast against the actual tab fill: white on the selected (blue) tab,
    // light grey otherwise.
    p.setPen(isChecked() ? QColor(255, 255, 255) : QColor(214, 219, 226));
    p.drawText(textRect, Qt::AlignCenter | Qt::TextSingleLine, text());

    p.restore();
}
