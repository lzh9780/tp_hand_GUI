#include "LogPanel.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextEdit>
#include <QTableWidget>
#include <QHeaderView>
#include <QCheckBox>
#include <QPushButton>
#include <QComboBox>
#include <QDateTime>
#include <QVariant>
#include <QScrollBar>
#include <QFont>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextCursor>
#include <QTextCharFormat>

namespace
{
/* Level tag width is fixed so the messages line up in a column. */
QString levelTag(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Debug:   return QStringLiteral("DEBUG");
    case LogLevel::Info:    return QStringLiteral("INFO ");
    case LogLevel::Warning: return QStringLiteral("WARN ");
    case LogLevel::Error:   return QStringLiteral("ERROR");
    }
    return QStringLiteral("INFO ");
}

int levelRank(LogLevel level)
{
    return static_cast<int>(level);
}

/* Telemetry columns: one per signal, so the table has five columns and a single
   value row. Kept in one place so the table and the update code agree. */
enum TelemetryColumn
{
    ColVoltage = 0,
    ColCurrent,
    ColTemp1,
    ColTemp2,
    ColFault,
    TelemetryColumnCount
};

/* Unit appended to each reading; empty for the fault code. */
const char *kTelemetryUnits[TelemetryColumnCount] = {"V", "mA", "C", "C", ""};
} // namespace

LogPanel::LogPanel(QWidget *parent)
    : QFrame(parent)
{
    setProperty("floatingPanel", QVariant(true));
    buildUi();
}

QColor LogPanel::colorFor(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Debug:   return QColor(150, 158, 170);
    case LogLevel::Info:    return QColor(214, 219, 226);
    case LogLevel::Warning: return QColor(240, 190, 90);
    case LogLevel::Error:   return QColor(240, 120, 120);
    }
    return QColor(214, 219, 226);
}

void LogPanel::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 4, 6, 6);
    root->setSpacing(4);

    /* ---------------- header ---------------- */
    auto *header = new QWidget(this);
    auto *headerLyt = new QHBoxLayout(header);
    headerLyt->setContentsMargins(0, 0, 0, 0);
    headerLyt->setSpacing(6);

    auto *title = new QLabel(tr("Log"), header);

    auto *levelLabel = new QLabel(tr("Level:"), header);
    levelFilter_ = new QComboBox(header);
    levelFilter_->addItem(tr("Debug"), QVariant(int(LogLevel::Debug)));
    levelFilter_->addItem(tr("Info"), QVariant(int(LogLevel::Info)));
    levelFilter_->addItem(tr("Warning"), QVariant(int(LogLevel::Warning)));
    levelFilter_->addItem(tr("Error"), QVariant(int(LogLevel::Error)));
    levelFilter_->setCurrentIndex(0);
    levelFilter_->setToolTip(tr("Hide messages below this level"));
    levelFilter_->setFixedHeight(22);

    autoScroll_ = new QCheckBox(tr("Follow"), header);
    autoScroll_->setChecked(true);
    autoScroll_->setToolTip(tr("Scroll to the newest line automatically"));

    clearBtn_ = new QPushButton(tr("Clear"), header);
    clearBtn_->setFixedHeight(22);

    headerLyt->addWidget(title);
    headerLyt->addStretch(1);
    headerLyt->addWidget(levelLabel);
    headerLyt->addWidget(levelFilter_);
    headerLyt->addWidget(autoScroll_);
    headerLyt->addWidget(clearBtn_);

    /* ---------------- text log ---------------- */
    logView_ = new QTextEdit(this);
    logView_->setReadOnly(true);
    // logView_->setLineWrapMode(QTextEdit::NoWrap);
    // A fixed-pitch font keeps the timestamp / level / message columns aligned.
    QFont mono(QStringLiteral("Monospace"));
    mono.setStyleHint(QFont::TypeWriter);
    mono.setPointSizeF(9.0);
    logView_->setFont(mono);

    /* ---------------- bus telemetry table ----------------
       One column per signal, so the whole bus state reads across in a single
       glance instead of as a tall list:
         | Bus voltage | Bus current | Temp 1 | Temp 2 | Fault |
         |   24.03 V   |    412 mA   | 31.20 C| 30.85 C| none  |
       Row 0 holds the value (with its unit appended), row 1 is a compact row of
       headers inside the table body. */
    auto *telemetryTitle = new QLabel(tr("Bus telemetry"), this);

    telemetry_ = new QTableWidget(/*rows=*/1, TelemetryColumnCount, this);
    // One column per signal; the names are the column headers.
    telemetry_->setHorizontalHeaderLabels(
        {tr("Voltage"), tr("Current"), tr("Temp 1"), tr("Temp 2"), tr("Fault")});
    telemetry_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    telemetry_->horizontalHeader()->setFixedHeight(22);
    telemetry_->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);

    telemetry_->verticalHeader()->setVisible(false);
    telemetry_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    telemetry_->setSelectionMode(QAbstractItemView::NoSelection);
    telemetry_->setFocusPolicy(Qt::NoFocus);
    telemetry_->setShowGrid(true);

    // Only the value row is used: the unit travels with the value so no extra
    // column is needed.
    for (int c = 0; c < TelemetryColumnCount; ++c)
    {
        auto *value = new QTableWidgetItem(QStringLiteral("--"));
        value->setTextAlignment(Qt::AlignCenter);
        telemetry_->setItem(0, c, value);
    }
    telemetry_->setRowHeight(0, 22);

    // The table only ever shows one row: size it to its content.
    telemetry_->setFixedHeight(telemetry_->horizontalHeader()->height() +
                               22 + 2 * telemetry_->frameWidth() + 2);

    root->addWidget(header);
    root->addWidget(logView_, 1);
    root->addWidget(telemetryTitle);
    root->addWidget(telemetry_);

    /* ---------------- wiring ---------------- */
    connect(clearBtn_, &QPushButton::clicked, this, &LogPanel::clearLog);
    connect(levelFilter_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int index)
            {
                setMinimumLevel(static_cast<LogLevel>(
                    levelFilter_->itemData(index).toInt()));
            });

    clearBusTelemetry();
}

void LogPanel::setMinimumLevel(LogLevel level)
{
    minimumLevel_ = level;
    // Re-render from the retained history: this hides the lines below the new
    // threshold and brings them back when the level is lowered again.
    rebuildView();
}

void LogPanel::append(LogLevel level, const QString &message)
{
    // Keep the full history so the level filter can be re-applied later.
    history_.append({level, message});

    if (levelRank(level) < levelRank(minimumLevel_))
        return;

    writeLine(level, message);

    if (autoScroll_->isChecked())
    {
        QScrollBar *sb = logView_->verticalScrollBar();
        sb->setValue(sb->maximum());
    }
}

/* Format and append one line to the text view. */
void LogPanel::writeLine(LogLevel level, const QString &message)
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    const QString line = QStringLiteral("%1 [%2] %3").arg(stamp, levelTag(level), message);

    QTextCharFormat fmt;
    fmt.setForeground(colorFor(level));
    if (level == LogLevel::Error)
        fmt.setFontWeight(QFont::Bold);

    QTextCursor cursor(logView_->document());
    cursor.movePosition(QTextCursor::End);
    if (!logView_->document()->isEmpty())
        cursor.insertBlock();
    cursor.insertText(line, fmt);
}

/* Rebuild the whole text view from `history_`, applying the level filter. */
void LogPanel::rebuildView()
{
    logView_->clear();
    for (const Entry &e : history_)
    {
        if (levelRank(e.level) < levelRank(minimumLevel_))
            continue;
        writeLine(e.level, e.message);
    }

    if (autoScroll_->isChecked())
    {
        QScrollBar *sb = logView_->verticalScrollBar();
        sb->setValue(sb->maximum());
    }
}

void LogPanel::append(const QString &message)
{
    append(LogLevel::Info, message);
}

void LogPanel::clearLog()
{
    logView_->clear();
    history_.clear();
}

void LogPanel::setBusTelemetry(double voltage_V, int current_mA,
                               double temp1_C, double temp2_C,
                               quint16 fault_code, bool fault_active)
{
    const QColor normal(214, 219, 226);
    const QColor bad = colorFor(LogLevel::Error);

    // One cell per signal; the unit is part of the value so the reading is
    // unambiguous without a unit column.
    setCell(ColVoltage, QStringLiteral("%1 %2").arg(voltage_V, 0, 'f', 2)
                            .arg(QString::fromLatin1(kTelemetryUnits[ColVoltage])), normal);
    setCell(ColCurrent, QStringLiteral("%1 %2").arg(current_mA)
                            .arg(QString::fromLatin1(kTelemetryUnits[ColCurrent])), normal);
    setCell(ColTemp1, QStringLiteral("%1 %2").arg(temp1_C, 0, 'f', 2)
                          .arg(QString::fromLatin1(kTelemetryUnits[ColTemp1])), normal);
    setCell(ColTemp2, QStringLiteral("%1 %2").arg(temp2_C, 0, 'f', 2)
                          .arg(QString::fromLatin1(kTelemetryUnits[ColTemp2])), normal);

    // The fault cell is the one that must stand out when something is wrong.
    if (fault_active)
        setCell(ColFault, QStringLiteral("0x%1").arg(fault_code, 0, 16), bad);
    else
        setCell(ColFault, tr("none"), normal);
}

void LogPanel::clearBusTelemetry()
{
    const QColor dim(120, 128, 140);
    for (int c = 0; c < TelemetryColumnCount; ++c)
        setCell(c, QStringLiteral("--"), dim);
}

void LogPanel::setCell(int column, const QString &text, const QColor &color)
{
    QTableWidgetItem *item = telemetry_->item(0, column);
    if (!item)
    {
        item = new QTableWidgetItem;
        item->setTextAlignment(Qt::AlignCenter);
        telemetry_->setItem(0, column, item);
    }
    item->setText(text);
    item->setForeground(color);
}
