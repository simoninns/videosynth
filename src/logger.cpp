/*
 * File:        logger.cpp
 * Module:      logger
 * Purpose:     Provides logging abstractions and spdlog-backed implementation.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "videosynth/logger.h"

#include <vector>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace videosynth {

namespace {

spdlog::level::level_enum ToSpdlogLevel(LogLevel level) {
  switch (level) {
    case LogLevel::kDebug:
      return spdlog::level::debug;
    case LogLevel::kTrace:
      return spdlog::level::trace;
    case LogLevel::kInfo:
    default:
      return spdlog::level::info;
  }
}

}  // namespace

SpdlogLogger::SpdlogLogger(LogLevel level, const std::string& log_file) {
  std::vector<spdlog::sink_ptr> sinks;
  sinks.push_back(std::make_shared<spdlog::sinks::stderr_color_sink_mt>());
  if (!log_file.empty()) {
    sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file, true));
  }

  logger_ = std::make_shared<spdlog::logger>("videosynth", sinks.begin(), sinks.end());
  logger_->set_level(ToSpdlogLevel(level));
  logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
}

void SpdlogLogger::Info(const std::string& message) { logger_->info(message); }

void SpdlogLogger::Error(const std::string& message) { logger_->error(message); }

void SpdlogLogger::Debug(const std::string& message) { logger_->debug(message); }

void SpdlogLogger::Trace(const std::string& message) { logger_->trace(message); }

}  // namespace videosynth
