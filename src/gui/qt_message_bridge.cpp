/*
 * File:        qt_message_bridge.cpp
 * Module:      gui
 * Purpose:     Routes Qt's own log messages into the application logger and
 *              suppresses known Wayland/XCB platform noise.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "qt_message_bridge.h"

#include <QMessageLogContext>
#include <QString>
#include <atomic>
#include <string>

namespace videosynth::gui {

namespace {

// The bridge target. Only the pointer is shared; the pointee must be
// thread-safe and outlive the installed handler.
std::atomic<ILogger*> g_bridge_logger{nullptr};

void HandleQtMessage(QtMsgType type, const QMessageLogContext& context,
                     const QString& message) {
  const char* category = context.category != nullptr ? context.category : "";
  if (ShouldSuppressQtMessage(type, category, message)) {
    return;
  }
  ILogger* logger = g_bridge_logger.load(std::memory_order_acquire);
  if (logger != nullptr) {
    LogQtMessage(*logger, type, category, message);
  }
}

}  // namespace

bool ShouldSuppressQtMessage(QtMsgType type, const char* category,
                             const QString& message) {
  // Never hide fatal diagnostics; they precede an abort.
  if (type == QtFatalMsg) {
    return false;
  }
  const QString cat = QString::fromLatin1(category != nullptr ? category : "");
  // Wayland/XCB platform (qpa) plugins emit frequent, benign integration
  // chatter: portal registration failures, text-input focus churn, pointer
  // grab notices. None are actionable in a desktop session.
  if (cat.startsWith(QStringLiteral("qt.qpa"))) {
    return true;
  }
  // The Wayland plugin logs this uncategorised on every popup window.
  if (message.contains(
          QStringLiteral("supports grabbing the mouse only for popup"))) {
    return true;
  }
  return false;
}

void LogQtMessage(ILogger& logger, QtMsgType type, const char* category,
                  const QString& message) {
  const QString cat = QString::fromLatin1(category != nullptr ? category : "");
  QString text = message;
  if (!cat.isEmpty() && cat != QStringLiteral("default")) {
    text = QLatin1Char('[') + cat + QStringLiteral("] ") + message;
  }
  const std::string line = "Qt: " + text.toStdString();
  switch (type) {
    case QtDebugMsg:
      logger.Debug(line);
      break;
    case QtInfoMsg:
      logger.Info(line);
      break;
    case QtWarningMsg:
      logger.Warning(line);
      break;
    case QtCriticalMsg:
    case QtFatalMsg:
      logger.Error(line);
      break;
  }
}

void InstallQtMessageBridge(ILogger* logger) {
  g_bridge_logger.store(logger, std::memory_order_release);
  // Passing nullptr restores Qt's default handler.
  qInstallMessageHandler(logger != nullptr ? &HandleQtMessage : nullptr);
}

}  // namespace videosynth::gui
