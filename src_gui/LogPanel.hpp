#pragma once

#include <QFrame>
#include <QString>
#include <QColor>
#include <QList>
#include "LogLevel.hpp"

class QTextEdit;
class QTableWidget;
class QCheckBox;
class QPushButton;
class QComboBox;

/**
 * LogPanel (GUI)
 * --------------
 * Right-column page that shows what the application is doing:
 *
 *   +-------------------------------------------+
 *   | Log / Bus telemetry      [level] [Clear]   |
 *   +-------------------------------------------+
 *   | 13:42:07.318 [INFO ] Device connected.     |
 *   | 13:42:07.402 [WARN ] Filter not enabled.   |
 *   | ...                                        |  <- QTextEdit (scrolling)
 *   +-------------------------------------------+
 *   | Bus telemetry                              |
 *   | +------+------+------+------+-----------+  |
 *   | |Bus V |Bus mA|Temp1 |Temp2 | Fault     |  |  <- QTableWidget
 *   | |24.03V| 412mA|31.20C|30.85C| none      |  |     (one column per
 *   | +------+------+------+------+-----------+  |      signal)
 *   +-------------------------------------------+
 *
 * Every log line carries a timestamp (HH:mm:ss.zzz) and a level tag, so a line
 * can be traced back to the moment it happened. The telemetry table is updated
 * in place rather than appended, so it is always the *current* bus state instead
 * of scrolling away like the text log would.
 *
 * The panel is a floating panel (property `floatingPanel`), matching the other
 * dock pages.
 */
class LogPanel : public QFrame
{
    Q_OBJECT
public:
    explicit LogPanel(QWidget *parent = nullptr);

public slots:
    /* Append a line with a timestamp and a level tag. */
    void append(LogLevel level, const QString &message);
    /* Convenience overload: informational line. */
    void append(const QString &message);

    /* Update the telemetry table in place (never appends). */
    void setBusTelemetry(double voltage_V, int current_mA,
                         double temp1_C, double temp2_C,
                         quint16 fault_code, bool fault_active);

    /* Mark the telemetry as stale / not connected (values shown as "--"). */
    void clearBusTelemetry();

    void clearLog();

    /* Level filter: below `level` messages are dropped. */
    void setMinimumLevel(LogLevel level);

private:
    /* One retained log line; kept so the level filter can be re-applied. */
    struct Entry
    {
        LogLevel level;
        QString message;
    };

    /* Colour used for a level's tag and text. */
    static QColor colorFor(LogLevel level);

    void buildUi();
    /* Format and append one line to the text view (no filtering, no history). */
    void writeLine(LogLevel level, const QString &message);
    /* Rebuild the text view from `history_` honouring the level filter. */
    void rebuildView();
    /* Write one telemetry cell (the table has a single value row). */
    void setCell(int column, const QString &text, const QColor &color);

    QTextEdit *logView_{nullptr};
    QTableWidget *telemetry_{nullptr};
    QComboBox *levelFilter_{nullptr};
    QPushButton *clearBtn_{nullptr};
    QCheckBox *autoScroll_{nullptr};

    QList<Entry> history_;
    LogLevel minimumLevel_{LogLevel::Debug};
};
