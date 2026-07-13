/*
 * File:        waveform_mapping.h
 * Module:      gui
 * Purpose:     Pure sample/time/level mapping helpers for the line waveform
 *              scope (no widget dependencies)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <cmath>
#include <vector>

#include "videosynth/model.h"
#include "videosynth/timing_constants.h"

namespace videosynth::gui {

// Thread-safety: all functions in this module are thread-safe pure functions.

// SMPTE 170M-2004 Section 12.1: 140 IRE spans the 1 V peak-to-peak signal,
// so 1 IRE = 1000/140 ≈ 7.143 mV.
constexpr double kMillivoltsPerIre = 1000.0 / 140.0;

inline double MillivoltsToIre(double millivolts) {
  return millivolts / kMillivoltsPerIre;
}

inline double IreToMillivolts(double ire) { return ire * kMillivoltsPerIre; }

inline double SampleIndexToMicroseconds(double sample_index,
                                        double sample_rate_hz) {
  if (sample_rate_hz <= 0.0) {
    return 0.0;
  }
  return sample_index / sample_rate_hz * 1.0e6;
}

inline int MicrosecondsToSampleIndex(double microseconds,
                                     double sample_rate_hz) {
  return static_cast<int>(std::lround(microseconds * 1.0e-6 * sample_rate_hz));
}

// One horizontal gridline of the waveform scope, anchored to a
// standard-defined signal level (see high-level-design.md §6.1).
struct SignalLevelAnchor {
  double millivolts = 0.0;
  const char* label = "";
};

// Gridline anchors for the standard's signal levels. PAL has zero setup
// (black == blanking) and yields three anchors; the System M standards yield
// four (sync tip, blanking, black setup, white) unless the project's black
// setup collapses black onto blanking.
inline std::vector<SignalLevelAnchor> SignalLevelAnchors(
    const SignalLevels& levels) {
  std::vector<SignalLevelAnchor> anchors;
  anchors.push_back({levels.sync_tip_mv, "sync"});
  anchors.push_back({levels.blanking_mv, "blanking"});
  if (levels.black_mv != levels.blanking_mv) {
    anchors.push_back({levels.black_mv, "black"});
  }
  anchors.push_back({levels.white_mv, "white"});
  return anchors;
}

// Vertical mV range the scope displays: the standard's levels plus headroom
// for chroma excursions around sync and above white.
struct PlotRange {
  double min_mv = 0.0;
  double max_mv = 0.0;
};

inline PlotRange DefaultPlotRange(const SignalLevels& levels) {
  constexpr double kHeadroomMv = 150.0;
  return PlotRange{levels.sync_tip_mv - kHeadroomMv,
                   levels.white_mv + kHeadroomMv};
}

// Maps a signal level to a plot y coordinate (0 = top edge = max_mv).
inline double MillivoltsToPlotY(double millivolts, const PlotRange& range,
                                double plot_height) {
  const double span = range.max_mv - range.min_mv;
  if (span <= 0.0 || plot_height <= 0.0) {
    return 0.0;
  }
  return (range.max_mv - millivolts) / span * plot_height;
}

inline double PlotYToMillivolts(double plot_y, const PlotRange& range,
                                double plot_height) {
  const double span = range.max_mv - range.min_mv;
  if (plot_height <= 0.0) {
    return range.max_mv;
  }
  return range.max_mv - (plot_y / plot_height) * span;
}

// Maps a sample index within a line to a plot x coordinate and back.
inline double SampleToPlotX(double sample_index, int line_sample_count,
                            double plot_width) {
  if (line_sample_count <= 1) {
    return 0.0;
  }
  return sample_index / static_cast<double>(line_sample_count - 1) * plot_width;
}

inline int PlotXToSample(double plot_x, int line_sample_count,
                         double plot_width) {
  if (line_sample_count <= 1 || plot_width <= 0.0) {
    return 0;
  }
  const double sample =
      plot_x / plot_width * static_cast<double>(line_sample_count - 1);
  const int rounded = static_cast<int>(std::lround(sample));
  if (rounded < 0) {
    return 0;
  }
  if (rounded > line_sample_count - 1) {
    return line_sample_count - 1;
  }
  return rounded;
}

}  // namespace videosynth::gui
