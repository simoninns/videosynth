/*
 * File:        forwarding_logger.cpp
 * Module:      gui
 * Purpose:     ILogger decorator forwarding messages to a GUI log callback
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "forwarding_logger.h"

#include <utility>

namespace videosynth::gui {

ForwardingLogger::ForwardingLogger(ILogger* inner, LogLevel level,
                                   ForwardFn forward)
    : inner_(inner), level_(level), forward_(std::move(forward)) {}

void ForwardingLogger::Info(const std::string& message) {
  inner_->Info(message);
  Forward(LogSeverity::kInfo, message);
}

void ForwardingLogger::Warning(const std::string& message) {
  inner_->Warning(message);
  Forward(LogSeverity::kWarning, message);
}

void ForwardingLogger::Error(const std::string& message) {
  inner_->Error(message);
  Forward(LogSeverity::kError, message);
}

void ForwardingLogger::Debug(const std::string& message) {
  inner_->Debug(message);
  if (level_ == LogLevel::kDebug || level_ == LogLevel::kTrace) {
    Forward(LogSeverity::kDebug, message);
  }
}

void ForwardingLogger::Trace(const std::string& message) {
  inner_->Trace(message);
  if (level_ == LogLevel::kTrace) {
    Forward(LogSeverity::kTrace, message);
  }
}

void ForwardingLogger::Forward(LogSeverity severity,
                               const std::string& message) const {
  if (forward_) {
    forward_(severity, message);
  }
}

}  // namespace videosynth::gui
