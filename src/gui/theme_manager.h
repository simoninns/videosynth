/*
 * File:        theme_manager.h
 * Module:      gui
 * Purpose:     Theme mode parsing, persistence, and colour-scheme resolution
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <QPalette>
#include <QSettings>
#include <QString>
#include <Qt>

namespace videosynth::gui {

// Pure theme-mode logic: string parsing, QSettings persistence, and
// resolution of a requested mode against the system colour scheme.
//
// Thread-safety: all members are static and stateless; they are safe to call
// from any thread. QSettings arguments follow QSettings' own threading rules
// (an instance must only be used from the thread that created it).
class ThemeManager {
 public:
  enum class Mode {
    kAuto,
    kLight,
    kDark,
  };

  // Outcome of resolving a mode against the system scheme.
  struct Resolution {
    Mode mode;
    Qt::ColorScheme scheme;
    bool is_dark;
    bool used_palette_fallback;
    QString source;
  };

  // QSettings key under which the theme mode is persisted.
  static constexpr const char* kSettingsKey = "view/theme_mode";

  // Parses a mode string ("auto", "light", "dark"; case-insensitive,
  // surrounding whitespace ignored). Unrecognised or empty input yields
  // kAuto; when `ok` is non-null it reports whether the input was valid.
  static Mode ModeFromString(const QString& text, bool* ok = nullptr);

  // Returns the canonical lower-case name for a mode.
  static QString ModeToString(Mode mode);

  // Returns "light", "dark", or "unknown" for logging.
  static QString ColorSchemeToString(Qt::ColorScheme scheme);

  // Auto mode follows the OS scheme, so only it needs live change tracking.
  static bool ShouldTrackSystemChanges(Mode mode);

  // Resolves a mode using the system-reported scheme and, when that scheme
  // is unknown, a palette-luminance fallback. Pure function of its inputs.
  //
  // Args:
  //   mode: Requested theme mode.
  //   system_scheme: Scheme reported by QStyleHints (may be Unknown).
  //   palette_is_dark: Result of IsPaletteDark on the application palette,
  //     consulted only when system_scheme is Unknown in auto mode.
  static Resolution ResolveScheme(Mode mode, Qt::ColorScheme system_scheme,
                                  bool palette_is_dark);

  // Heuristic: a palette is dark when the window colour is darker than the
  // window text colour.
  static bool IsPaletteDark(const QPalette& palette);

  // Reads the persisted mode; missing or invalid values yield kAuto.
  static Mode LoadMode(const QSettings& settings);

  // Persists the mode under kSettingsKey.
  static void SaveMode(QSettings* settings, Mode mode);
};

}  // namespace videosynth::gui
