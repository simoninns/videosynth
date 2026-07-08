/*
 * File:        logger.h
 * Module:      logger
 * Purpose:     Provides logging abstractions and spdlog-backed implementation.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <spdlog/logger.h>

#include <memory>
#include <string>

#include "videosynth/interfaces.h"

namespace videosynth {

enum class LogLevel {
  kInfo,
  kDebug,
  kTrace,
};

// Thread-safety: SpdlogLogger IS thread-safe. The underlying spdlog::logger
// is designed for concurrent use from multiple threads. All logging methods
// (Info, Warning, Error, Debug, Trace) may be called concurrently. The
// logger is constructed directly with multi-threaded (_mt) sinks and is NOT
// registered in spdlog's global registry, so instances carry no hidden
// global or main-thread state and may be created and used on any thread.
class SpdlogLogger final : public ILogger {
 public:
  // Constructs a logger with the specified level and optional log file.
  //
  // Args:
  //   level: Minimum log level to output.
  //   log_file: Path to log file. If empty, logs to stdout.
  SpdlogLogger(LogLevel level, const std::string& log_file);

  // Logs an info-level message.
  //
  // Args:
  //   message: The message to log.
  void Info(const std::string& message) override;

  // Logs a warning-level message.
  //
  // Args:
  //   message: The message to log.
  void Warning(const std::string& message) override;

  // Logs an error-level message.
  //
  // Args:
  //   message: The message to log.
  void Error(const std::string& message) override;

  // Logs a debug-level message.
  //
  // Args:
  //   message: The message to log.
  void Debug(const std::string& message) override;

  // Logs a trace-level message.
  //
  // Args:
  //   message: The message to log.
  void Trace(const std::string& message) override;

 private:
  std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace videosynth
