#include "SideDock.hpp"
#include "DockTabButton.hpp"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTabWidget>
#include <QTabBar>
#include <QStyle>
#include <QWidget>
#include <QVariant>
#include <QSizePolicy>

SideDock::SideDock(Qt::Edge edge, QWidget *parent)
    : QFrame(parent), edge_(edge)
{
    // The dock is only a container: it must be fully transparent so the 3D view
    // stays visible behind the gaps. Each page paints its own background.
    setObjectName("sideDock");
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);

    // Pages are stored in a QTabWidget so it can own them; its own tab bar is
    // hidden because the visible tabs are the rotated buttons built below.
    tabs_ = new QTabWidget(this);
    tabs_->tabBar()->hide();
    tabs_->setDocumentMode(true);
    tabs_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    // ---- tab strip (rotated real tabs) ----
    // No fixed width: the strip sizes itself to the widest tab so the titles
    // are shown in full, and the buttons are centred inside it.
    stripHost_ = new QWidget(this);
    stripHost_->setObjectName("dockTabStrip");
    stripHost_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    // The strip must not paint: the gaps between the rotated tabs have to show
    // the 3D view. A plain QWidget only honours a style-sheet background when
    // WA_StyledBackground is set, so without it the strip would be filled by the
    // default widget brush (the flat window colour seen as a black column).
    stripHost_->setAttribute(Qt::WA_StyledBackground, true);
    stripHost_->setAutoFillBackground(false);

    stripLyt_ = new QVBoxLayout(stripHost_);
    stripLyt_->setContentsMargins(0, 0, 0, 0);
    stripLyt_->setSpacing(0); // real tabs sit flush against each other
    // Tabs are inserted before this trailing stretch, keeping them at the top.
    stripLyt_->addStretch(1);

    // ---- content + strip: strip on the inner side of the column ----
    // The strip keeps its slot even while the content is hidden, so collapsing a
    // column never moves the tabs to the opposite edge. `tabs_` is therefore not
    // removed from the layout (which would shift the strip); it only gets a zero
    // width when collapsed.
    rootLyt_ = new QHBoxLayout(this);
    rootLyt_->setContentsMargins(0, 0, 0, 0);
    rootLyt_->setSpacing(0);

    if (edge_ == Qt::LeftEdge)
    {
        // left dock: content at the window edge, tabs towards the centre
        rootLyt_->addWidget(tabs_, 1);
        rootLyt_->addWidget(stripHost_);
    }
    else
    {
        // right dock: tabs towards the centre, content at the window edge
        rootLyt_->addWidget(stripHost_);
        rootLyt_->addWidget(tabs_, 1);
    }

    updateCollapseState();
}

int SideDock::addPanel(QWidget *page, const QString &title, const QString &tooltip)
{
    if (!page)
        return -1;

    // Every page carries the translucent panel look itself, so the dock around
    // them can stay fully transparent.
    //
    // The properties are set before the widget is inserted, but the style sheet
    // was already applied at construction: a dynamic property only takes effect
    // after the widget is re-polished. Without the explicit polish below the
    // page would keep its unpainted (black) background instead of matching
    // *[floatingPanel="true"].
    page->setProperty("dockPage", QVariant(true));
    page->setProperty("floatingPanel", QVariant(true));
    const int idx = tabs_->addTab(page, title);
    if (!tooltip.isEmpty())
        tabs_->setTabToolTip(idx, tooltip);

    // Re-apply the style sheet so the new property values are honoured.
    page->style()->unpolish(page);
    page->style()->polish(page);
    page->update();

    rebuildTabs();
    if (tabs_->currentIndex() < 0)
        selectIndex(0);
    return idx;
}

void SideDock::rebuildTabs()
{
    const int n = tabs_->count();

    // Grow the button list to match the page count.
    while (tabButtons_.size() < n)
    {
        // Left dock: text reads top-to-bottom. Right dock: bottom-to-top, so
        // both columns lean towards their own window edge consistently.
        auto *btn = new DockTabButton(QString(), edge_ == Qt::LeftEdge, stripHost_);
        const int id = tabButtons_.size();

        // Insert before the trailing stretch so tabs stay packed at the top.
        stripLyt_->insertWidget(stripLyt_->count() - 1, btn);
        connect(btn, &QAbstractButton::clicked, this,
                [this, id]() { onTabClicked(id); });

        tabButtons_.append(btn);
    }

    // Shrink when pages were removed.
    while (tabButtons_.size() > n)
    {
        DockTabButton *btn = tabButtons_.takeLast();
        stripLyt_->removeWidget(btn);
        btn->deleteLater();
    }

    for (int i = 0; i < n; ++i)
    {
        DockTabButton *btn = tabButtons_[i];
        btn->setText(tabs_->tabText(i));
        // QStyle draws the first / last / middle tabs differently.
        btn->setTabPosition(i, n);
        const QString tip = tabs_->tabToolTip(i);
        btn->setToolTip(tip.isEmpty() ? tabs_->tabText(i) : tip);
    }

    // The strip adapts to the widest tab so no title is ever elided.
    int stripWidth = 0;
    for (DockTabButton *btn : tabButtons_)
        stripWidth = qMax(stripWidth, btn->sizeHint().width());
    if (stripWidth > 0)
        stripHost_->setFixedWidth(stripWidth);

    updateTabStates();
    updateCollapseState();
}

void SideDock::updateTabStates()
{
    // A tab is highlighted only while its panel is actually shown. When the
    // column is collapsed every page is hidden, so no tab may look selected:
    // a highlighted tab next to a hidden panel would wrongly suggest that the
    // panel is open. Selecting a page again re-highlights its tab through
    // selectIndex() -> updateTabStates().
    for (int i = 0; i < tabButtons_.size(); ++i)
        tabButtons_[i]->setChecked(!collapsed_ && i == tabs_->currentIndex());
}

void SideDock::onTabClicked(int index)
{
    // Clicking the tab that is already shown hides the content; clicking any
    // other tab selects it (which also restores a hidden column).
    if (index == tabs_->currentIndex() && !collapsed_ && collapseOnTabClick_)
        setCollapsed(true);
    else
        selectIndex(index);
}

void SideDock::selectIndex(int index)
{
    if (index < 0 || index >= tabs_->count())
        return;

    // Selecting a page always brings the content back.
    if (collapsed_)
        setCollapsed(false);

    if (tabs_->currentIndex() != index)
    {
        tabs_->setCurrentIndex(index);
        updateTabStates();
        syncPageVisibility(); // exactly one page visible
        emit currentPanelChanged(tabs_->widget(index));
    }
}

/*
 * Mutual exclusivity of the pages.
 *
 * QTabWidget shows only its current page, but the page widgets are also toggled
 * from updateCollapseState() when the column is hidden, and a page added while
 * the dock was collapsed could be left visible on top of the current one. Making
 * the rule explicit removes any doubt about the resulting state.
 */
void SideDock::syncPageVisibility()
{
    const int current = tabs_->currentIndex();
    for (int i = 0; i < tabs_->count(); ++i)
    {
        if (QWidget *page = tabs_->widget(i))
        {
            const bool show = !collapsed_ && (i == current);
            if (page->isVisible() != show)
                page->setVisible(show);
        }
    }
}

void SideDock::showPanel(QWidget *page)
{
    const int idx = indexOf(page);
    if (idx >= 0)
        selectIndex(idx);
}

int SideDock::indexOf(QWidget *page) const
{
    return page ? tabs_->indexOf(page) : -1;
}

int SideDock::count() const
{
    return tabs_->count();
}

void SideDock::clearPanels()
{
    while (tabs_->count() > 0)
        tabs_->removeTab(0);
    rebuildTabs();
}

void SideDock::setPanelTitle(QWidget *page, const QString &title)
{
    const int idx = indexOf(page);
    if (idx < 0)
        return;
    tabs_->setTabText(idx, title);
    rebuildTabs();
}

void SideDock::setDockWidth(int px)
{
    dockWidth_ = qMax(120, px);
    updateCollapseState();
}

int SideDock::currentWidth() const
{
    if (!collapsed_)
        return dockWidth_;
    // Collapsed: just wide enough for the tab strip. The layout margins are 0
    // and the content is squeezed to zero width, so the strip is the whole dock.
    return stripHost_->width() +
           rootLyt_->contentsMargins().left() +
           rootLyt_->contentsMargins().right() +
           rootLyt_->spacing();
}

void SideDock::setCollapsed(bool collapsed)
{
    if (collapsed_ == collapsed)
        return;
    collapsed_ = collapsed;
    updateCollapseState();
    emit collapsedChanged(collapsed_);
}

void SideDock::updateCollapseState()
{
    // When collapsed only the content disappears: the strip stays in place so
    // the tabs remain clickable and can bring the panel back.
    //
    // The content widget is squeezed to zero width rather than hidden: hiding it
    // would remove it from the layout, which would slide the tab strip to the
    // opposite edge of the column.
    if (collapsed_)
    {
        tabs_->setMaximumWidth(0);
        tabs_->setMinimumWidth(0);
    }
    else
    {
        tabs_->setMaximumWidth(QWIDGETSIZE_MAX);
        tabs_->setMinimumWidth(0);
    }

    // Note: the dock does NOT resize itself. It is floated on top of the 3D view
    // and placed by MainWidget::recomputeGeometry(), which polls currentWidth().
    // Calling setFixedWidth() here would fight that geometry pass.
    updateGeometry();

    // Show the current page and hide every other one (or hide them all while the
    // column is collapsed).
    syncPageVisibility();

    // Keep every tab visible in both states, but only the shown panel's tab is
    // highlighted: collapsing the column un-highlights every tab so the strip
    // does not advertise a panel that is currently hidden.
    for (DockTabButton *btn : tabButtons_)
        btn->setVisible(true);
    updateTabStates();
}
