/*
 * File:        theme_color_tokens.h
 * Module:      gui
 * Purpose:     Shared colour tokens for theme-aware custom painting
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <QColor>
#include <QPalette>
#include <QtGlobal>

namespace videosynth::gui::theme_tokens {

// Colour roles used by custom-painted preview widgets (picture views and the
// line waveform scope). Primary tokens are for the selected trace; secondary
// tokens are for overlaid comparison traces.
enum class PlotColorToken {
  kLumaPrimary,
  kChromaPrimary,
  kLumaSecondary,
  kChromaSecondary,
  kCompositePrimary,
  kCompositeSecondary,
  kRegionBurst,
  kRegionActiveVideo,
  kMarkerSelection,
  kFieldBoundary,
};

// Linear RGBA blend between two colours; ratio is clamped to [0, 1].
inline QColor Blend(const QColor& from, const QColor& to, qreal ratio) {
  const auto clamped = static_cast<float>(qBound<qreal>(0.0, ratio, 1.0));
  return QColor::fromRgbF(
      from.redF() + (to.redF() - from.redF()) * clamped,
      from.greenF() + (to.greenF() - from.greenF()) * clamped,
      from.blueF() + (to.blueF() - from.blueF()) * clamped,
      from.alphaF() + (to.alphaF() - from.alphaF()) * clamped);
}

// De-emphasised text colour taken from the palette's disabled group.
inline QColor MutedText(const QPalette& palette) {
  return palette.color(QPalette::Disabled, QPalette::WindowText);
}

// Neutral line colour between window background and text; emphasis in [0, 1]
// moves the result towards the text colour.
inline QColor NeutralLine(const QPalette& palette, qreal emphasis) {
  return Blend(palette.color(QPalette::Window),
               palette.color(QPalette::WindowText), emphasis);
}

// Gridline colour for signal-level anchors (sync tip, blanking, white).
inline QColor GridLine(const QPalette& palette) {
  return NeutralLine(palette, 0.25);
}

// Trace and marker colours for the resolved theme.
inline QColor PlotColor(PlotColorToken token, bool dark_theme) {
  switch (token) {
    case PlotColorToken::kLumaPrimary:
      return dark_theme ? QColor(255, 255, 100) : QColor(200, 180, 0);
    case PlotColorToken::kChromaPrimary:
      return dark_theme ? QColor(100, 150, 255) : QColor(0, 80, 200);
    case PlotColorToken::kLumaSecondary:
      return dark_theme ? QColor(255, 255, 180) : QColor(230, 210, 40);
    case PlotColorToken::kChromaSecondary:
      return dark_theme ? QColor(160, 190, 255) : QColor(80, 120, 220);
    case PlotColorToken::kCompositePrimary:
      return dark_theme ? QColor(100, 200, 255) : QColor(0, 100, 200);
    case PlotColorToken::kCompositeSecondary:
      return dark_theme ? QColor(255, 255, 100) : QColor(200, 180, 0);
    case PlotColorToken::kRegionBurst:
      return QColor(0, 255, 255);
    case PlotColorToken::kRegionActiveVideo:
    case PlotColorToken::kFieldBoundary:
      return PlotColor(PlotColorToken::kLumaPrimary, dark_theme);
    case PlotColorToken::kMarkerSelection:
      return QColor(0, 255, 0);
  }

  return dark_theme ? QColor(255, 255, 255) : QColor(0, 0, 0);
}

// Text colour for validation issue severities in lists and status displays.
inline QColor IssueSeverityColor(bool is_error, bool dark_theme) {
  if (is_error) {
    return dark_theme ? QColor(255, 120, 120) : QColor(180, 0, 0);
  }
  return dark_theme ? QColor(255, 200, 100) : QColor(160, 100, 0);
}

}  // namespace videosynth::gui::theme_tokens
