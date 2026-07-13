/*
 * File:        test_generation_preferences.cpp
 * Module:      gui_tests
 * Purpose:     Tests for generation preferences: RunOptions mapping (unit)
 *              and QSettings persistence round-trip (functional, real INI
 *              file)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <QSettings>
#include <QString>
#include <QTemporaryDir>

#include "generation_preferences.h"

namespace videosynth::gui {
namespace {

// --- Pure mapping (unit) ----------------------------------------------------

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

// --- QSettings persistence (functional) --------------------------------------

class GenerationPreferencesSettingsTest : public ::testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(temp_dir_.isValid()); }

  QString SettingsPath() const {
    return temp_dir_.filePath(QStringLiteral("preferences_test.ini"));
  }

  QTemporaryDir temp_dir_;
};

TEST_F(GenerationPreferencesSettingsTest, SaveAndLoadRoundTrips) {
  GenerationPreferences saved;
  saved.threads = 8;
  saved.log_level = QStringLiteral("trace");
  saved.log_to_file = true;
  saved.log_file_path = QStringLiteral("/tmp/videosynth.log");

  {
    QSettings settings(SettingsPath(), QSettings::IniFormat);
    SaveGenerationPreferences(saved, &settings);
    settings.sync();
  }

  const QSettings reloaded(SettingsPath(), QSettings::IniFormat);
  EXPECT_EQ(LoadGenerationPreferences(reloaded), saved);
}

TEST_F(GenerationPreferencesSettingsTest, LoadReturnsDefaultsWhenMissing) {
  const QSettings settings(SettingsPath(), QSettings::IniFormat);
  const GenerationPreferences loaded = LoadGenerationPreferences(settings);
  EXPECT_EQ(loaded, GenerationPreferences{});
  EXPECT_EQ(loaded.threads, 0);
  EXPECT_EQ(loaded.log_level, QStringLiteral("info"));
  EXPECT_FALSE(loaded.log_to_file);
}

TEST_F(GenerationPreferencesSettingsTest, LoadSanitisesStoredValues) {
  {
    QSettings settings(SettingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("generation/threads"), -2);
    settings.setValue(QStringLiteral("generation/log_level"),
                      QStringLiteral("chatty"));
    settings.sync();
  }

  const QSettings settings(SettingsPath(), QSettings::IniFormat);
  const GenerationPreferences loaded = LoadGenerationPreferences(settings);
  EXPECT_EQ(loaded.threads, 0);
  EXPECT_EQ(loaded.log_level, QStringLiteral("info"));
}

}  // namespace
}  // namespace videosynth::gui
