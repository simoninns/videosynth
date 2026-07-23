/*
 * File:        logging_options.cpp
 * Module:      gui
 * Purpose:     Command-line logging options shared with the CLI front-end
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "logging_options.h"

namespace videosynth::gui {

namespace {

bool IsValidLogLevel(const QString& log_level) {
  return log_level == QLatin1String("info") ||
         log_level == QLatin1String("debug") ||
         log_level == QLatin1String("trace");
}

// Matches `argument` against "--<name> <value>"/"--<name>=<value>". Returns
// true when the option was consumed; false with a non-empty `error` when the
// value is missing. `index` is advanced past a separate value argument.
bool MatchValueOption(const QStringList& arguments, const QString& name,
                      qsizetype* index, QString* value, QString* error) {
  const QString& argument = arguments.at(*index);
  const QString prefix = QStringLiteral("--") + name;
  if (argument == prefix) {
    if (*index + 1 >= arguments.size()) {
      *error = QStringLiteral("Missing value for --%1.").arg(name);
      return false;
    }
    *value = arguments.at(++(*index));
    return true;
  }
  if (argument.startsWith(prefix + QLatin1Char('='))) {
    *value = argument.mid(prefix.size() + 1);
    return true;
  }
  return false;
}

}  // namespace

bool ParseLoggingOptions(const QStringList& arguments, LoggingOptions* options,
                         QString* error) {
  error->clear();
  // Index 0 is the program name when a full argv list is supplied; it can
  // never match an option so the loop starts at 0 for simplicity.
  for (qsizetype i = 0; i < arguments.size(); ++i) {
    QString value;
    if (MatchValueOption(arguments, QStringLiteral("log-level"), &i, &value,
                         error)) {
      if (!IsValidLogLevel(value)) {
        *error = QStringLiteral(
                     "Invalid --log-level '%1'; expected info, debug, or "
                     "trace.")
                     .arg(value);
        return false;
      }
      options->log_level = value;
      continue;
    }
    if (!error->isEmpty()) {
      return false;
    }
    if (MatchValueOption(arguments, QStringLiteral("log-file"), &i, &value,
                         error)) {
      options->log_file = value;
      continue;
    }
    if (!error->isEmpty()) {
      return false;
    }
  }
  return true;
}

LogLevel ToLogLevel(const QString& log_level) {
  if (log_level == QLatin1String("debug")) {
    return LogLevel::kDebug;
  }
  if (log_level == QLatin1String("trace")) {
    return LogLevel::kTrace;
  }
  return LogLevel::kInfo;
}

RunOptions ApplyLoggingOverrides(RunOptions options,
                                 const LoggingOptions& overrides) {
  if (!overrides.log_level.isEmpty()) {
    options.log_level = overrides.log_level.toStdString();
  }
  if (!overrides.log_file.isEmpty()) {
    options.log_file = overrides.log_file.toStdString();
  }
  return options;
}

}  // namespace videosynth::gui
