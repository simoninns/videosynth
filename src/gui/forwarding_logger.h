/*
 * File:        forwarding_logger.h
 * Module:      gui
 * Purpose:     ILogger decorator forwarding messages to a GUI log callback
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <functional>
#include <string>

#include "videosynth/interfaces.h"
#include "videosynth/logger.h"

namespace videosynth::gui {

// Severity of a forwarded log message, ordered from most to least severe.
// Shared by the forwarding logger and the GUI log model.
enum class LogSeverity {
  kError,
  kWarning,
  kInfo,
  kDebug,
  kTrace,
};

// Decorates an ILogger: every message is forwarded to the inner logger
// unchanged and, when it passes the configured level filter, also delivered
// to a callback (severity, message). The level filter mirrors the CLI logger
// semantics: Info/Warning/Error always pass, Debug requires kDebug or
// kTrace, Trace requires kTrace.
//
// Thread-safety: ForwardingLogger IS thread-safe provided the inner logger
// and the callback are thread-safe (the callback is invoked concurrently
// from any logging thread). The decorator itself holds only immutable state
// after construction.
class ForwardingLogger final : public ILogger {
 public:
  using ForwardFn =
      std::function<void(LogSeverity severity, const std::string& message)>;

  // Ownership: does not take ownership of `inner`; it must outlive this
  // logger and must not be null.
  ForwardingLogger(ILogger* inner, LogLevel level, ForwardFn forward);

  void Info(const std::string& message) override;
  void Warning(const std::string& message) override;
  void Error(const std::string& message) override;
  void Debug(const std::string& message) override;
  void Trace(const std::string& message) override;

 private:
  void Forward(LogSeverity severity, const std::string& message) const;

  ILogger* inner_;
  LogLevel level_;
  ForwardFn forward_;
};

}  // namespace videosynth::gui
