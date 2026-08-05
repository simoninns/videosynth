/*
 * File:        test_generation_preferences_settings.cpp
 * Module:      gui_tests
 * Purpose:     Round-trips generation preferences through a real QSettings INI
 *              file
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
