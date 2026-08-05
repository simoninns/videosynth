/*
 * File:        test_qt_message_bridge.cpp
 * Module:      gui_tests
 * Purpose:     Unit tests for Qt message suppression and logger forwarding
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <QString>
#include <QtGlobal>
#include <string>
#include <vector>

#include "qt_message_bridge.h"
#include "videosynth/interfaces.h"

namespace videosynth::gui {
namespace {

// Records which ILogger method received which message, so a forwarded Qt
// message can be asserted without touching real logging sinks.
class RecordingLogger final : public ILogger {
 public:
  struct Entry {
    std::string level;
    std::string message;
  };

  void Info(const std::string& message) override {
    entries_.push_back({"info", message});
  }
  void Warning(const std::string& message) override {
    entries_.push_back({"warning", message});
  }
  void Error(const std::string& message) override {
    entries_.push_back({"error", message});
  }
  void Debug(const std::string& message) override {
    entries_.push_back({"debug", message});
  }
  void Trace(const std::string& message) override {
    entries_.push_back({"trace", message});
  }

  const std::vector<Entry>& entries() const { return entries_; }

 private:
  std::vector<Entry> entries_;
};

TEST(QtMessageBridgeTest, SuppressesQpaPlatformCategories) {
  EXPECT_TRUE(ShouldSuppressQtMessage(QtWarningMsg, "qt.qpa.services",
                                      QStringLiteral("portal failure")));
  EXPECT_TRUE(ShouldSuppressQtMessage(QtWarningMsg, "qt.qpa.wayland.textinput",
                                      QStringLiteral("leave event")));
}

TEST(QtMessageBridgeTest, SuppressesUncategorisedMouseGrabNotice) {
  EXPECT_TRUE(ShouldSuppressQtMessage(
      QtWarningMsg, "",
      QStringLiteral(
          "This plugin supports grabbing the mouse only for popup windows")));
}

TEST(QtMessageBridgeTest, KeepsGenuineApplicationWarnings) {
  EXPECT_FALSE(ShouldSuppressQtMessage(QtWarningMsg, "js",
                                       QStringLiteral("QML binding loop")));
  EXPECT_FALSE(ShouldSuppressQtMessage(QtCriticalMsg, "default",
                                       QStringLiteral("real problem")));
}

TEST(QtMessageBridgeTest, NeverSuppressesFatalMessages) {
  EXPECT_FALSE(ShouldSuppressQtMessage(QtFatalMsg, "qt.qpa.services",
                                       QStringLiteral("fatal in qpa")));
}

TEST(QtMessageBridgeTest, MapsSeverityToMatchingLoggerMethod) {
  RecordingLogger logger;
  LogQtMessage(logger, QtDebugMsg, "", QStringLiteral("d"));
  LogQtMessage(logger, QtInfoMsg, "", QStringLiteral("i"));
  LogQtMessage(logger, QtWarningMsg, "", QStringLiteral("w"));
  LogQtMessage(logger, QtCriticalMsg, "", QStringLiteral("c"));

  ASSERT_EQ(logger.entries().size(), 4U);
  EXPECT_EQ(logger.entries()[0].level, "debug");
  EXPECT_EQ(logger.entries()[1].level, "info");
  EXPECT_EQ(logger.entries()[2].level, "warning");
  EXPECT_EQ(logger.entries()[3].level, "error");
}

TEST(QtMessageBridgeTest, PrefixesMeaningfulCategoryButNotDefault) {
  RecordingLogger logger;
  LogQtMessage(logger, QtWarningMsg, "js", QStringLiteral("binding"));
  LogQtMessage(logger, QtWarningMsg, "default", QStringLiteral("plain"));
  LogQtMessage(logger, QtWarningMsg, "", QStringLiteral("empty"));

  ASSERT_EQ(logger.entries().size(), 3U);
  EXPECT_EQ(logger.entries()[0].message, "Qt: [js] binding");
  EXPECT_EQ(logger.entries()[1].message, "Qt: plain");
  EXPECT_EQ(logger.entries()[2].message, "Qt: empty");
}

}  // namespace
}  // namespace videosynth::gui
