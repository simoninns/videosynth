/*
 * File:        test_logging_options.cpp
 * Module:      gui_tests
 * Purpose:     Tests for command-line logging option parsing and RunOptions
 *              overrides (unit)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <QString>
#include <QStringList>

#include "logging_options.h"

namespace videosynth::gui {
namespace {

TEST(LoggingOptionsTest, ParseAcceptsSeparateValueForm) {
  LoggingOptions options;
  QString error;
  const QStringList arguments{
      QStringLiteral("videosynth-gui"), QStringLiteral("--log-level"),
      QStringLiteral("debug"), QStringLiteral("--log-file"),
      QStringLiteral("/tmp/gui.log")};
  ASSERT_TRUE(ParseLoggingOptions(arguments, &options, &error));
  EXPECT_TRUE(error.isEmpty());
  EXPECT_EQ(options.log_level, QStringLiteral("debug"));
  EXPECT_EQ(options.log_file, QStringLiteral("/tmp/gui.log"));
}

TEST(LoggingOptionsTest, ParseAcceptsEqualsForm) {
  LoggingOptions options;
  QString error;
  const QStringList arguments{QStringLiteral("--log-level=trace"),
                              QStringLiteral("--log-file=/tmp/gui.log")};
  ASSERT_TRUE(ParseLoggingOptions(arguments, &options, &error));
  EXPECT_EQ(options.log_level, QStringLiteral("trace"));
  EXPECT_EQ(options.log_file, QStringLiteral("/tmp/gui.log"));
}

TEST(LoggingOptionsTest, ParseLeavesUnsuppliedOptionsEmpty) {
  LoggingOptions options;
  QString error;
  ASSERT_TRUE(ParseLoggingOptions({QStringLiteral("videosynth-gui")}, &options,
                                  &error));
  EXPECT_TRUE(options.log_level.isEmpty());
  EXPECT_TRUE(options.log_file.isEmpty());
}

TEST(LoggingOptionsTest, ParseIgnoresUnrelatedArguments) {
  LoggingOptions options;
  QString error;
  const QStringList arguments{
      QStringLiteral("videosynth-gui"), QStringLiteral("-style"),
      QStringLiteral("fusion"), QStringLiteral("--log-level"),
      QStringLiteral("info")};
  ASSERT_TRUE(ParseLoggingOptions(arguments, &options, &error));
  EXPECT_EQ(options.log_level, QStringLiteral("info"));
}

TEST(LoggingOptionsTest, ParseRejectsUnknownLevel) {
  LoggingOptions options;
  QString error;
  const QStringList arguments{QStringLiteral("--log-level"),
                              QStringLiteral("verbose")};
  EXPECT_FALSE(ParseLoggingOptions(arguments, &options, &error));
  EXPECT_FALSE(error.isEmpty());
}

TEST(LoggingOptionsTest, ParseRejectsMissingValues) {
  LoggingOptions options;
  QString error;
  EXPECT_FALSE(
      ParseLoggingOptions({QStringLiteral("--log-level")}, &options, &error));
  EXPECT_FALSE(error.isEmpty());

  error.clear();
  EXPECT_FALSE(
      ParseLoggingOptions({QStringLiteral("--log-file")}, &options, &error));
  EXPECT_FALSE(error.isEmpty());
}

TEST(LoggingOptionsTest, ParseKeepsLastValueWhenRepeated) {
  LoggingOptions options;
  QString error;
  const QStringList arguments{
      QStringLiteral("--log-level"), QStringLiteral("debug"),
      QStringLiteral("--log-level"), QStringLiteral("trace")};
  ASSERT_TRUE(ParseLoggingOptions(arguments, &options, &error));
  EXPECT_EQ(options.log_level, QStringLiteral("trace"));
}

TEST(LoggingOptionsTest, ToLogLevelMapsAllLevels) {
  EXPECT_EQ(ToLogLevel(QStringLiteral("info")), LogLevel::kInfo);
  EXPECT_EQ(ToLogLevel(QStringLiteral("debug")), LogLevel::kDebug);
  EXPECT_EQ(ToLogLevel(QStringLiteral("trace")), LogLevel::kTrace);
  EXPECT_EQ(ToLogLevel(QString()), LogLevel::kInfo);
  EXPECT_EQ(ToLogLevel(QStringLiteral("bogus")), LogLevel::kInfo);
}

TEST(LoggingOptionsTest, ApplyLoggingOverridesReplacesSuppliedFields) {
  RunOptions options;
  options.log_level = "info";
  options.log_file = "/tmp/preferences.log";

  LoggingOptions overrides;
  overrides.log_level = QStringLiteral("trace");
  overrides.log_file = QStringLiteral("/tmp/cli.log");

  const RunOptions result = ApplyLoggingOverrides(options, overrides);
  EXPECT_EQ(result.log_level, "trace");
  EXPECT_EQ(result.log_file, "/tmp/cli.log");
}

TEST(LoggingOptionsTest, ApplyLoggingOverridesKeepsUnsuppliedFields) {
  RunOptions options;
  options.log_level = "debug";
  options.log_file = "/tmp/preferences.log";

  const RunOptions result = ApplyLoggingOverrides(options, LoggingOptions{});
  EXPECT_EQ(result.log_level, "debug");
  EXPECT_EQ(result.log_file, "/tmp/preferences.log");
}

}  // namespace
}  // namespace videosynth::gui
