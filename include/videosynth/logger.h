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

class SpdlogLogger final : public ILogger {
 public:
  SpdlogLogger(LogLevel level, const std::string& log_file);

  void Info(const std::string& message) override;
  void Error(const std::string& message) override;
  void Debug(const std::string& message) override;
  void Trace(const std::string& message) override;

 private:
  std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace videosynth
