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

// Headroom above white for the standard range. Saturated chroma carries the
// composite well past white: 100% colour bars peak at 933 mV for PAL
// (ITU-R BT.1700 Annex 1 Part B) and at 131 IRE = 936 mV for NTSC
// (SMPTE 170M-2004 Section 12.3), so 300 mV clears both with margin.
constexpr double kStandardCeilingHeadroomMv = 300.0;

// Headroom below sync tip for the standard range. The same colour bars trough
// at -233 mV (PAL) and -33 IRE = -236 mV (NTSC), which sits above sync tip;
// the headroom instead covers chroma riding on sync-level content and filter
// undershoot around the sync edges. Excursions that deliberately go further
// below sync tip need PlotRangeMode::kSubSync.
constexpr double kStandardFloorHeadroomMv = 250.0;

inline PlotRange DefaultPlotRange(const SignalLevels& levels) {
  return PlotRange{levels.sync_tip_mv - kStandardFloorHeadroomMv,
                   levels.white_mv + kStandardCeilingHeadroomMv};
}

// Selectable vertical ranges of the scope. The standard range spans every
// valid PAL and NTSC composite level, but still cuts off signals that
// deliberately swing below sync tip: the PAL pilot burst swings +/-300 mV
// about sync tip, so its troughs reach -600 mV (see model.h
// IsSubSyncCapableSampleEncodingPreset).
enum class PlotRangeMode {
  kStandard,        // all valid composite levels, with headroom
  kSubSync,         // widened floor for pilot-burst troughs below sync tip
  kBlankingDetail,  // zoomed about blanking for burst and VITS levels
  kFit,             // fitted to the plotted samples
};

// Sub-sync floor headroom: the PAL pilot burst troughs at 300 mV below sync
// tip (ITU-R BT.1700 Annex 1 Part B Table 2), plus 100 mV of margin.
constexpr double kSubSyncHeadroomMv = 400.0;

// Half-span about blanking for the zoomed range: clears the 300 mV p-p PAL
// colour burst (ITU-R BT.1700 Annex 1 Part B Table 2 item 5) and the VITS
// level bars with room to spare.
constexpr double kBlankingDetailHalfSpanMv = 400.0;

// Fitted range around the plotted samples' extremes. `min_mv` above `max_mv`
// means there is nothing to plot, which falls back to the standard range.
inline PlotRange FitPlotRange(const SignalLevels& levels, double min_mv,
                              double max_mv) {
  // A flat trace still needs a usable vertical span, and a fitted trace should
  // not touch the plot edges.
  constexpr double kMinimumSpanMv = 100.0;
  constexpr double kPadFraction = 0.08;
  if (min_mv > max_mv) {
    return DefaultPlotRange(levels);
  }
  if (max_mv - min_mv < kMinimumSpanMv) {
    const double centre_mv = (min_mv + max_mv) / 2.0;
    min_mv = centre_mv - kMinimumSpanMv / 2.0;
    max_mv = centre_mv + kMinimumSpanMv / 2.0;
  }
  const double padding_mv = (max_mv - min_mv) * kPadFraction;
  return PlotRange{min_mv - padding_mv, max_mv + padding_mv};
}

// Vertical range for a mode. `min_mv`/`max_mv` are the plotted samples'
// extremes and are only used by kFit.
inline PlotRange PlotRangeForMode(PlotRangeMode mode,
                                  const SignalLevels& levels, double min_mv,
                                  double max_mv) {
  switch (mode) {
    case PlotRangeMode::kSubSync:
      return PlotRange{levels.sync_tip_mv - kSubSyncHeadroomMv,
                       DefaultPlotRange(levels).max_mv};
    case PlotRangeMode::kBlankingDetail:
      return PlotRange{levels.blanking_mv - kBlankingDetailHalfSpanMv,
                       levels.blanking_mv + kBlankingDetailHalfSpanMv};
    case PlotRangeMode::kFit:
      return FitPlotRange(levels, min_mv, max_mv);
    case PlotRangeMode::kStandard:
      break;
  }
  return DefaultPlotRange(levels);
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
