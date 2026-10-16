#pragma once

#include <QFrame>
#include <QString>
#include <QList>

class QTabWidget;
class QVBoxLayout;
class QHBoxLayout;
class QWidget;
class DockTabButton;

/**
 * SideDock (GUI)
 * --------------
 * Panel column docked at the left or the right edge of the main window.
 *
 * The tab strip sits on the **inner** side of the column, i.e. next to the
 * content and towards the centre of the window; the content hugs the outer
 * window edge:
 *
 *   left dock                  right dock
 *   +---------------+------+   +------+---------------+
 *   |               | Joi  |   | Plot |               |
 *   |  active       | nts  |   | s    |   active      |
 *   |  panel        | Trac |   | Tact |   panel       |
 *   |               | king |   | ile  |               |
 *   +---------------+------+   +------+---------------+
 *    content        tabs        tabs     content
 *
 * Tabs are real Qt tabs (painted by QStyle through DockTabButton) with their
 * labels rotated 90°, so a tab is a normal tab turned on its side. The strip is
 * exactly as wide as the widest tab needs, so titles are never elided.
 *
 * Hiding: there is no separate hide button. Clicking the tab that is *already
 * shown* hides the content, leaving the strip in place; clicking any tab brings
 * the content back. MainWindow can also collapse the column programmatically.
 *
 * The dock is a floating panel (property `floatingPanel`), so the global style
 * sheet gives it the shared semi-transparent background.
 *
 * Callers hand over already-created widgets: the dock re-parents them into its
 * stack and never deletes them (MainWindow owns them).
 */
class SideDock : public QFrame
{
    Q_OBJECT
public:
    /* Qt::LeftEdge (dock at the left window edge) or Qt::RightEdge. */
    explicit SideDock(Qt::Edge edge, QWidget *parent = nullptr);

    /* Append a page. Ownership stays with the caller. Returns its index. */
    int addPanel(QWidget *page, const QString &title, const QString &tooltip = QString());

    /* Bring `page` to the front (restores the dock from the hidden state). */
    void showPanel(QWidget *page);
    /* Index of `page`, or -1. */
    int indexOf(QWidget *page) const;
    /* Number of registered pages. */
    int count() const;
    /* Remove every page (the widgets themselves survive). */
    void clearPanels();

    /* Title shown on the tab of `page`. */
    void setPanelTitle(QWidget *page, const QString &title);

    /* Width the whole column occupies when expanded. */
    int dockWidth() const { return dockWidth_; }
    void setDockWidth(int px);

    /* Width the column wants right now: dockWidth() when expanded, or just the
       tab strip when collapsed. The dock does not resize itself — the owner
       (MainWidget) places it, so the 3D view underneath is never affected. */
    int currentWidth() const;

    /* Hide / restore the content. The tab strip always stays visible, so the
       panel can be brought back by clicking any of its tabs. */
    void setCollapsed(bool collapsed);
    bool isCollapsed() const { return collapsed_; }
    void setCollapseOnTabClick(bool on) { collapseOnTabClick_ = on; }

signals:
    /* Current page changed (used to lazy-start timers etc.). */
    void currentPanelChanged(QWidget *page);
    /* Layout-relevant state change: the column width just changed. */
    void collapsedChanged(bool collapsed);

private:
    void selectIndex(int index);
    void onTabClicked(int index);
    void rebuildTabs();
    void updateTabStates();
    void updateCollapseState();
    /* Show the current page, hide all others (or hide them all if collapsed). */
    void syncPageVisibility();

    Qt::Edge edge_;
    QTabWidget *tabs_{nullptr};   // owns the pages, its own tab bar is hidden
    QWidget *stripHost_{nullptr}; // container for the rotated tab buttons
    QVBoxLayout *stripLyt_{nullptr};
    QHBoxLayout *rootLyt_{nullptr};

    QList<DockTabButton *> tabButtons_;

    int dockWidth_{340};
    bool collapsed_{false};
    bool collapseOnTabClick_{true};
};
