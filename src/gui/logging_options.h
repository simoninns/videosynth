/*
 * File:        logging_options.h
 * Module:      gui
 * Purpose:     Command-line logging options shared with the CLI front-end
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <QString>
#include <QStringList>

#include "videosynth/interfaces.h"
#include "videosynth/logger.h"

namespace videosynth::gui {

// Logging configuration taken from the command line. An empty field means
// the option was not supplied and the corresponding behaviour keeps its
// default (application log at "info", generation runs per preferences).
struct LoggingOptions {
  // "info", "debug", or "trace"; empty when --log-level was not supplied.
  QString log_level;
  // Log file path; empty when --log-file was not supplied.
  QString log_file;
};

// Extracts --log-level/--log-file from `arguments` (both "--option value"
// and "--option=value" forms; a repeated option keeps the last value, the
// same as the CLI). Unrelated arguments are ignored so Qt's own options
// still reach QCommandLineParser. Returns false and sets `error` when a
// value is missing or the level is not info/debug/trace.
//
// Ownership: `options` and `error` must be valid (non-null); the caller
// retains ownership.
//
// Thread-safety: thread-safe (pure function).
bool ParseLoggingOptions(const QStringList& arguments, LoggingOptions* options,
                         QString* error);

// Maps a log-level string to LogLevel; empty or unknown values fall back to
// kInfo, mirroring the CLI default.
//
// Thread-safety: thread-safe (pure function).
LogLevel ToLogLevel(const QString& log_level);

// Returns `options` with any supplied command-line logging fields applied,
// so generation runs honour --log-level/--log-file over the persisted
// preferences for the lifetime of the session. Unsupplied fields leave
// `options` unchanged.
//
// Thread-safety: thread-safe (pure function).
RunOptions ApplyLoggingOverrides(RunOptions options,
                                 const LoggingOptions& overrides);

}  // namespace videosynth::gui
