/*
 * File:        test_theme_manager.cpp
 * Module:      gui_tests
 * Purpose:     Unit tests for ThemeManager mode parsing and colour-scheme
 *              resolution (no QApplication required).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <QPalette>
#include <QString>

#include "theme_manager.h"

namespace videosynth::gui {
namespace {

using Mode = ThemeManager::Mode;

// ---------------------------------------------------------------------------
// ModeFromString() tests
// ---------------------------------------------------------------------------

TEST(ThemeManagerTest, ModeFromStringParsesCanonicalNames) {
  EXPECT_EQ(ThemeManager::ModeFromString(QStringLiteral("auto")), Mode::kAuto);
  EXPECT_EQ(ThemeManager::ModeFromString(QStringLiteral("light")),
            Mode::kLight);
  EXPECT_EQ(ThemeManager::ModeFromString(QStringLiteral("dark")), Mode::kDark);
}

TEST(ThemeManagerTest, ModeFromStringIgnoresCaseAndWhitespace) {
  EXPECT_EQ(ThemeManager::ModeFromString(QStringLiteral("  DaRk \t")),
            Mode::kDark);
  EXPECT_EQ(ThemeManager::ModeFromString(QStringLiteral("LIGHT")),
            Mode::kLight);
  EXPECT_EQ(ThemeManager::ModeFromString(QStringLiteral(" Auto ")),
            Mode::kAuto);
}

TEST(ThemeManagerTest, ModeFromStringReportsValidInput) {
  bool ok = false;
  ThemeManager::ModeFromString(QStringLiteral("dark"), &ok);
  EXPECT_TRUE(ok);
}

TEST(ThemeManagerTest, ModeFromStringFallsBackToAutoOnInvalidInput) {
  bool ok = true;
  EXPECT_EQ(ThemeManager::ModeFromString(QStringLiteral("solarized"), &ok),
            Mode::kAuto);
  EXPECT_FALSE(ok);

  ok = true;
  EXPECT_EQ(ThemeManager::ModeFromString(QString(), &ok), Mode::kAuto);
  EXPECT_FALSE(ok);
}

// ---------------------------------------------------------------------------
// ModeToString() round-trip tests
// ---------------------------------------------------------------------------

TEST(ThemeManagerTest, ModeStringConversionRoundTripsAllModes) {
  for (const Mode mode : {Mode::kAuto, Mode::kLight, Mode::kDark}) {
    bool ok = false;
    EXPECT_EQ(
        ThemeManager::ModeFromString(ThemeManager::ModeToString(mode), &ok),
        mode);
    EXPECT_TRUE(ok);
  }
}

// ---------------------------------------------------------------------------
// ResolveScheme() tests
// ---------------------------------------------------------------------------

TEST(ThemeManagerTest, ResolveSchemeForcesLightRegardlessOfSystem) {
  const ThemeManager::Resolution resolution = ThemeManager::ResolveScheme(
      Mode::kLight, Qt::ColorScheme::Dark, /*palette_is_dark=*/true);
  EXPECT_EQ(resolution.scheme, Qt::ColorScheme::Light);
  EXPECT_FALSE(resolution.is_dark);
  EXPECT_FALSE(resolution.used_palette_fallback);
}

TEST(ThemeManagerTest, ResolveSchemeForcesDarkRegardlessOfSystem) {
  const ThemeManager::Resolution resolution = ThemeManager::ResolveScheme(
      Mode::kDark, Qt::ColorScheme::Light, /*palette_is_dark=*/false);
  EXPECT_EQ(resolution.scheme, Qt::ColorScheme::Dark);
  EXPECT_TRUE(resolution.is_dark);
  EXPECT_FALSE(resolution.used_palette_fallback);
}

TEST(ThemeManagerTest, ResolveSchemeAutoFollowsSystemScheme) {
  const ThemeManager::Resolution dark = ThemeManager::ResolveScheme(
      Mode::kAuto, Qt::ColorScheme::Dark, /*palette_is_dark=*/false);
  EXPECT_TRUE(dark.is_dark);
  EXPECT_FALSE(dark.used_palette_fallback);

  const ThemeManager::Resolution light = ThemeManager::ResolveScheme(
      Mode::kAuto, Qt::ColorScheme::Light, /*palette_is_dark=*/true);
  EXPECT_FALSE(light.is_dark);
  EXPECT_FALSE(light.used_palette_fallback);
}

TEST(ThemeManagerTest, ResolveSchemeAutoUsesPaletteFallbackWhenUnknown) {
  const ThemeManager::Resolution dark = ThemeManager::ResolveScheme(
      Mode::kAuto, Qt::ColorScheme::Unknown, /*palette_is_dark=*/true);
  EXPECT_EQ(dark.scheme, Qt::ColorScheme::Dark);
  EXPECT_TRUE(dark.is_dark);
  EXPECT_TRUE(dark.used_palette_fallback);

  const ThemeManager::Resolution light = ThemeManager::ResolveScheme(
      Mode::kAuto, Qt::ColorScheme::Unknown, /*palette_is_dark=*/false);
  EXPECT_EQ(light.scheme, Qt::ColorScheme::Light);
  EXPECT_FALSE(light.is_dark);
  EXPECT_TRUE(light.used_palette_fallback);
}

// ---------------------------------------------------------------------------
// System tracking and palette heuristic tests
// ---------------------------------------------------------------------------

TEST(ThemeManagerTest, OnlyAutoModeTracksSystemChanges) {
  EXPECT_TRUE(ThemeManager::ShouldTrackSystemChanges(Mode::kAuto));
  EXPECT_FALSE(ThemeManager::ShouldTrackSystemChanges(Mode::kLight));
  EXPECT_FALSE(ThemeManager::ShouldTrackSystemChanges(Mode::kDark));
}

TEST(ThemeManagerTest, IsPaletteDarkComparesWindowAndTextLightness) {
  QPalette dark_palette;
  dark_palette.setColor(QPalette::Window, QColor(30, 30, 30));
  dark_palette.setColor(QPalette::WindowText, QColor(230, 230, 230));
  EXPECT_TRUE(ThemeManager::IsPaletteDark(dark_palette));

  QPalette light_palette;
  light_palette.setColor(QPalette::Window, QColor(240, 240, 240));
  light_palette.setColor(QPalette::WindowText, QColor(20, 20, 20));
  EXPECT_FALSE(ThemeManager::IsPaletteDark(light_palette));
}

}  // namespace
}  // namespace videosynth::gui
