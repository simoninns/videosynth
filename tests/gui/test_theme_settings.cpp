/*
 * File:        test_theme_settings.cpp
 * Module:      gui_tests
 * Purpose:     Functional tests for theme-mode persistence through QSettings
 *              (uses a real INI file, so classified functional).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <QSettings>
#include <QString>
#include <QTemporaryDir>

#include "theme_manager.h"

namespace videosynth::gui {
namespace {

using Mode = ThemeManager::Mode;

class ThemeSettingsTest : public ::testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(temp_dir_.isValid()); }

  QString SettingsPath() const {
    return temp_dir_.filePath(QStringLiteral("theme_test.ini"));
  }

  QTemporaryDir temp_dir_;
};

TEST_F(ThemeSettingsTest, SaveAndLoadRoundTripsAllModes) {
  for (const Mode mode : {Mode::kAuto, Mode::kLight, Mode::kDark}) {
    {
      QSettings settings(SettingsPath(), QSettings::IniFormat);
      ThemeManager::SaveMode(&settings, mode);
      settings.sync();
    }

    const QSettings reloaded(SettingsPath(), QSettings::IniFormat);
    EXPECT_EQ(ThemeManager::LoadMode(reloaded), mode);
  }
}

TEST_F(ThemeSettingsTest, LoadReturnsAutoWhenKeyIsMissing) {
  const QSettings settings(SettingsPath(), QSettings::IniFormat);
  EXPECT_EQ(ThemeManager::LoadMode(settings), Mode::kAuto);
}

TEST_F(ThemeSettingsTest, LoadReturnsAutoWhenStoredValueIsInvalid) {
  {
    QSettings settings(SettingsPath(), QSettings::IniFormat);
    settings.setValue(QLatin1String(ThemeManager::kSettingsKey),
                      QStringLiteral("sepia"));
    settings.sync();
  }

  const QSettings reloaded(SettingsPath(), QSettings::IniFormat);
  EXPECT_EQ(ThemeManager::LoadMode(reloaded), Mode::kAuto);
}

}  // namespace
}  // namespace videosynth::gui
