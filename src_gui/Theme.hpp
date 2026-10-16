#pragma once

#include <QColor>
#include <QString>

/**
 * Theme (GUI)
 * -----------
 * Central definition of the dark theme shared by every panel.
 *
 * The main window background colour is also used as the semi-transparent
 * background of the floating panels (see UI design note #2), so both come
 * from the same source and stay in sync.
 *
 * Panels are rendered on top of the 3D model widget (the last layer of the
 * main window), therefore they must be translucent: `panelStyleSheet()` gives
 * every QFrame inside a floating panel the same rgba() background.
 */
namespace theme
{
/* Main window background. Panels reuse it with an alpha channel. */
inline QColor windowBackground() { return QColor(28, 30, 34); }

/* Opacity of the panel background, 0..255. */
inline int panelAlpha() { return 235; }

/* Panel background = window background + alpha. */
inline QColor panelBackground()
{
    QColor c = windowBackground();
    c.setAlpha(panelAlpha());
    return c;
}

inline QString rgba(const QColor &c)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(c.red())
        .arg(c.green())
        .arg(c.blue())
        .arg(c.alpha());
}

/*
 * Global application style sheet: dark Fusion palette + the translucent panel
 * rules. Panels mark themselves with the `floatingPanel` property so a single
 * rule covers all of them instead of every widget styling itself.
 */
inline QString appStyleSheet()
{
    const QColor bg = windowBackground();
    const QString panel = rgba(panelBackground());

    return QStringLiteral(R"(
QWidget {
    background-color: rgb(%1, %2, %3);
    color: #d8dbe0;
    font-size: 12px;
}
QMainWindow, QDialog {
    background-color: rgb(%1, %2, %3);
}
/* Any widget can be a floating panel, not only QFrame subclasses: the pages of
   a SideDock are a mix of QWidget / QFrame derived classes. */
QWidget[floatingPanel="true"] {
    background-color: %4;
    border: 1px solid rgba(120, 130, 145, 160);
    border-radius: 6px;
}
QWidget[floatingPanel="true"] QWidget {
    background: transparent;
}
QWidget[floatingPanel="true"] QLabel {
    background: transparent;
}
/* --- inputs --- */
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {
    background-color: rgba(20, 22, 26, 200);
    border: 1px solid rgba(110, 120, 135, 180);
    border-radius: 4px;
    padding: 2px 5px;
    selection-background-color: #3d6ea5;
}
QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled {
    color: #6b7280;
    background-color: rgba(40, 42, 46, 150);
}
QComboBox QAbstractItemView {
    background-color: rgb(38, 40, 45);
    border: 1px solid rgba(110, 120, 135, 180);
    selection-background-color: #3d6ea5;
}
/* --- buttons --- */
QPushButton, QToolButton {
    background-color: rgba(62, 68, 78, 220);
    border: 1px solid rgba(120, 130, 145, 190);
    border-radius: 4px;
    padding: 3px 10px;
}
QPushButton:hover, QToolButton:hover  { background-color: rgba(82, 90, 102, 230); }
QPushButton:pressed, QToolButton:pressed { background-color: rgba(48, 54, 62, 230); }
QPushButton:disabled, QToolButton:disabled {
    color: #6b7280;
    background-color: rgba(48, 52, 58, 160);
    border-color: rgba(90, 96, 106, 140);
}
QPushButton:checked, QToolButton:checked {
    background-color: rgba(61, 110, 165, 235);
    border-color: rgba(120, 170, 225, 220);
}
/* --- sliders --- */
QSlider::groove:horizontal {
    height: 6px;
    background: rgba(20, 22, 26, 200);
    border-radius: 3px;
}
QSlider::sub-page:horizontal {
    background: rgba(61, 110, 165, 230);
    border-radius: 3px;
}
QSlider::handle:horizontal {
    width: 14px;
    margin: -5px 0;
    border-radius: 7px;
    background: #b7c0cc;
}
QSlider::handle:horizontal:disabled { background: #5a6068; }
/* --- lists, tables, text --- */
QListWidget, QTableWidget, QTextEdit, QPlainTextEdit, QTextBrowser {
    background-color: rgba(18, 20, 24, 190);
    border: 1px solid rgba(110, 120, 135, 150);
    border-radius: 4px;
    gridline-color: rgba(90, 98, 110, 120);
    selection-background-color: #3d6ea5;
}
QHeaderView::section {
    background-color: rgba(52, 58, 68, 230);
    border: none;
    border-right: 1px solid rgba(90, 98, 110, 140);
    border-bottom: 1px solid rgba(90, 98, 110, 140);
    padding: 3px 5px;
}
QTableWidget::item:alternate { background-color: rgba(38, 42, 48, 170); }
/* --- vertical dock tab strip ---
   DockTabButton asks QStyle for the tab shape and paints its own label, so the
   strip must not paint at all: only the tab shapes should be visible, letting
   the 3D view and the columns show through the gaps between them.
   The global "QWidget { background-color: ... }" rule would otherwise fill the
   strip with the window colour, so it is overridden here explicitly. */
#dockTabStrip {
    background: transparent !important;
    border: none;
}
#dockTabStrip > QWidget {
    background: transparent !important;
    border: none;
}
/* The tab paints its own shape in paintEvent(); no background from the sheet. */
DockTabButton, #dockTab {
    background: transparent;
    border: none;
}
/* --- panel column container ---
   The dock that holds the tabs is a pure container and must not paint anything:
   otherwise its translucent plate would sit behind the pages and between the
   tabs. Each page paints its own translucent panel instead. */
#sideDock, #sideDock > QTabWidget {
    background: transparent;
    border: none;
}
/* QTabWidget's private page area would otherwise draw the widget background. */
#sideDock QTabWidget::pane {
    background: transparent;
    border: none;
}
/* --- scrollbars --- */
QScrollBar:vertical   { background: transparent; width: 10px; margin: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 0; }
QScrollBar::handle:vertical, QScrollBar::handle:horizontal {
    background: rgba(120, 130, 145, 180);
    border-radius: 5px;
    min-height: 24px;
    min-width: 24px;
}
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
/* --- misc --- */
QGroupBox {
    border: 1px solid rgba(100, 108, 120, 160);
    border-radius: 5px;
    margin-top: 10px;
    padding-top: 6px;
}
QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }
QSplitter::handle { background: rgba(70, 76, 86, 160); }
QStatusBar { background-color: rgba(24, 26, 30, 220); }
QToolTip {
    background-color: rgb(46, 50, 58);
    color: #d8dbe0;
    border: 1px solid rgba(120, 130, 145, 190);
}
)")
        .arg(bg.red())
        .arg(bg.green())
        .arg(bg.blue())
        .arg(panel);
}

} // namespace theme
