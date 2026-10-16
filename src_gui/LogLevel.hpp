#pragma once

/**
 * LogLevel
 * --------
 * Severity of a log line. Drives both the level tag that LogPanel writes in
 * front of every message and the colour it is rendered in.
 *
 * Lives in its own header so producers (MainWindow and friends) can use a level
 * without pulling in the LogPanel widget itself.
 */
enum class LogLevel
{
    Debug,
    Info,
    Warning,
    Error
};
