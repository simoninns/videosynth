/*
 * File:        waveform_scope_widget.h
 * Module:      gui
 * Purpose:     Line waveform scope plotting a selected line's samples in mV
 *              against standard signal-level gridlines
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <QWidget>
#include <vector>

#include "videosynth/timing_constants.h"
#include "waveform_mapping.h"

namespace videosynth::gui {

// Plots one video line's samples in millivolts. Horizontal gridlines anchor
// the standard's signal levels (sync tip, blanking, black setup, white);
// traces are selectable as composite (Y+C), Y, C, or a Y+C overlay of both
// channels. The vertical range is selectable too, so sub-sync excursions such
// as the PAL pilot burst stay on screen (see PlotRangeMode). Mouse movement
// reports the cursor position as sample index, µs, and mV through CursorMoved
// for the readout label.
//
// Thread-safety: NOT thread-safe. GUI (main) thread only.
class WaveformScopeWidget : public QWidget {
  Q_OBJECT

 public:
  enum class TraceMode {
    kComposite,  // single trace of Y+C
    kLuma,       // Y only
    kChroma,     // C only
    kOverlay,    // Y and C as separate overlaid traces
  };

  explicit WaveformScopeWidget(QWidget* parent = nullptr);

  // Replaces the plotted line. The vectors are per-sample mV values of the
  // Y and C channels for one line; empty vectors clear the plot.
  void SetLineData(std::vector<double> y_mv, std::vector<double> c_mv,
                   double sample_rate_hz, const SignalLevels& levels);

  void SetTraceMode(TraceMode mode);
  TraceMode trace_mode() const { return trace_mode_; }

  // Selects the plotted vertical range; kFit follows the current trace mode's
  // samples, so changing either recomputes the range.
  void SetRangeMode(PlotRangeMode mode);
  PlotRangeMode range_mode() const { return range_mode_; }
  PlotRange range() const { return range_; }

  // Dark/light trace palette (see theme_color_tokens.h).
  void SetDarkTheme(bool dark_theme);

  QSize minimumSizeHint() const override;

 signals:
  // Cursor position over the plot: sample index within the line, elapsed
  // microseconds from line start, and the level the cursor points at.
  void CursorMoved(int sample_index, double microseconds, double millivolts);

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void leaveEvent(QEvent* event) override;

 private:
  // Plot area inside the widget, leaving room for gridline labels.
  QRect PlotRect() const;
  void PaintTrace(QPainter* painter, const QRect& plot,
                  const std::vector<double>& trace_mv,
                  const QColor& color) const;
  // Per-sample sum of the Y and C channels (the composite trace).
  std::vector<double> CompositeMv() const;
  // Recomputes range_ from the range mode and, for kFit, the samples the
  // current trace mode plots.
  void RecomputeRange();

  std::vector<double> y_mv_;
  std::vector<double> c_mv_;
  double sample_rate_hz_ = 0.0;
  SignalLevels levels_ = {};
  PlotRange range_ = {};
  TraceMode trace_mode_ = TraceMode::kComposite;
  PlotRangeMode range_mode_ = PlotRangeMode::kStandard;
  bool dark_theme_ = false;
  int cursor_sample_ = -1;
};

}  // namespace videosynth::gui
