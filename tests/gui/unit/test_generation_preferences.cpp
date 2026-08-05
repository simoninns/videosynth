/*
 * File:        test_generation_preferences.cpp
 * Module:      gui_tests
 * Purpose:     Validates the pure preference-to-RunOptions mapping. QSettings
 *              persistence is covered by the functional suite in
 *              tests/gui/functional/test_generation_preferences_settings.cpp
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <QString>

#include "generation_preferences.h"

namespace videosynth::gui {
namespace {

TEST(GenerationPreferencesTest, SanitizedLogLevelAcceptsKnownLevels) {
  EXPECT_EQ(SanitizedLogLevel(QStringLiteral("info")), QStringLiteral("info"));
  EXPECT_EQ(SanitizedLogLevel(QStringLiteral("debug")),
            QStringLiteral("debug"));
  EXPECT_EQ(SanitizedLogLevel(QStringLiteral("trace")),
            QStringLiteral("trace"));
}

TEST(GenerationPreferencesTest, SanitizedLogLevelFallsBackToInfo) {
  EXPECT_EQ(SanitizedLogLevel(QStringLiteral("verbose")),
            QStringLiteral("info"));
  EXPECT_EQ(SanitizedLogLevel(QString()), QStringLiteral("info"));
}

TEST(GenerationPreferencesTest, MakeRunOptionsMapsAllFields) {
  GenerationPreferences preferences;
  preferences.threads = 4;
  preferences.log_level = QStringLiteral("debug");
  preferences.log_to_file = true;
  preferences.log_file_path = QStringLiteral("/tmp/run.log");

  const RunOptions options = MakeRunOptions(preferences);
  EXPECT_EQ(options.threads, 4);
  EXPECT_EQ(options.log_level, "debug");
  EXPECT_EQ(options.log_file, "/tmp/run.log");
  EXPECT_FALSE(options.validate_only);
  EXPECT_TRUE(options.project_path.empty());
}

TEST(GenerationPreferencesTest, MakeRunOptionsOmitsLogFileWhenDisabled) {
  GenerationPreferences preferences;
  preferences.log_to_file = false;
  preferences.log_file_path = QStringLiteral("/tmp/run.log");
  EXPECT_TRUE(MakeRunOptions(preferences).log_file.empty());

  preferences.log_to_file = true;
  preferences.log_file_path.clear();
  EXPECT_TRUE(MakeRunOptions(preferences).log_file.empty());
}

TEST(GenerationPreferencesTest, MakeRunOptionsSanitisesValues) {
  GenerationPreferences preferences;
  preferences.threads = -3;
  preferences.log_level = QStringLiteral("bogus");

  const RunOptions options = MakeRunOptions(preferences, true);
  EXPECT_EQ(options.threads, 0);
  EXPECT_EQ(options.log_level, "info");
  EXPECT_TRUE(options.validate_only);
}

}  // namespace
}  // namespace videosynth::gui
