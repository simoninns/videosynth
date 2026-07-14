/*
 * File:        waveform_scope_widget.cpp
 * Module:      gui
 * Purpose:     Line waveform scope plotting a selected line's samples in mV
 *              against standard signal-level gridlines
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "waveform_scope_widget.h"

#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cstddef>
#include <utility>

#include "theme_color_tokens.h"

namespace videosynth::gui {

namespace {

constexpr int kLabelMarginLeft = 64;
constexpr int kPlotMargin = 8;
// Doubled from the original 120 px so the line waveform reads at a glance in
// the preview dialog.
constexpr int kMinimumPlotHeight = 240;

}  // namespace

WaveformScopeWidget::WaveformScopeWidget(QWidget* parent) : QWidget(parent) {
  setMouseTracking(true);
  setMinimumHeight(kMinimumPlotHeight + 2 * kPlotMargin);
}

void WaveformScopeWidget::SetLineData(std::vector<double> y_mv,
                                      std::vector<double> c_mv,
                                      double sample_rate_hz,
                                      const SignalLevels& levels) {
  y_mv_ = std::move(y_mv);
  c_mv_ = std::move(c_mv);
  sample_rate_hz_ = sample_rate_hz;
  levels_ = levels;
  range_ = DefaultPlotRange(levels);
  update();
}

void WaveformScopeWidget::SetTraceMode(TraceMode mode) {
  if (trace_mode_ == mode) {
    return;
  }
  trace_mode_ = mode;
  update();
}

void WaveformScopeWidget::SetDarkTheme(bool dark_theme) {
  if (dark_theme_ == dark_theme) {
    return;
  }
  dark_theme_ = dark_theme;
  update();
}

QSize WaveformScopeWidget::minimumSizeHint() const {
  return QSize(320, kMinimumPlotHeight + 2 * kPlotMargin);
}

QRect WaveformScopeWidget::PlotRect() const {
  return rect().adjusted(kLabelMarginLeft, kPlotMargin, -kPlotMargin,
                         -kPlotMargin);
}

void WaveformScopeWidget::PaintTrace(QPainter* painter, const QRect& plot,
                                     const std::vector<double>& trace_mv,
                                     const QColor& color) const {
  if (trace_mv.size() < 2) {
    return;
  }

  QPainterPath path;
  const int sample_count = static_cast<int>(trace_mv.size());
  for (int i = 0; i < sample_count; ++i) {
    const double x = plot.left() + SampleToPlotX(i, sample_count, plot.width());
    const double y =
        plot.top() + MillivoltsToPlotY(trace_mv[static_cast<std::size_t>(i)],
                                       range_, plot.height());
    if (i == 0) {
      path.moveTo(x, y);
    } else {
      path.lineTo(x, y);
    }
  }
  painter->setPen(QPen(color, 1.0));
  painter->drawPath(path);
}

void WaveformScopeWidget::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.fillRect(rect(), palette().color(QPalette::Base));

  const QRect plot = PlotRect();
  if (!plot.isValid()) {
    return;
  }
  painter.setRenderHint(QPainter::Antialiasing, true);

  // Signal-level gridlines with labels (high-level-design.md §6.1 anchors).
  const QColor grid_color = theme_tokens::GridLine(palette());
  const QColor label_color = theme_tokens::MutedText(palette());
  for (const SignalLevelAnchor& anchor : SignalLevelAnchors(levels_)) {
    const double y = plot.top() + MillivoltsToPlotY(anchor.millivolts, range_,
                                                    plot.height());
    painter.setPen(QPen(grid_color, 1.0));
    painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    painter.setPen(label_color);
    painter.drawText(QRectF(0, y - 8.0, kLabelMarginLeft - 6.0, 16.0),
                     Qt::AlignRight | Qt::AlignVCenter,
                     QStringLiteral("%1").arg(anchor.millivolts, 0, 'f', 0));
  }

  // Traces.
  if (trace_mode_ == TraceMode::kComposite) {
    std::vector<double> composite(y_mv_.size(), 0.0);
    for (std::size_t i = 0; i < y_mv_.size(); ++i) {
      composite[i] = y_mv_[i] + (i < c_mv_.size() ? c_mv_[i] : 0.0);
    }
    PaintTrace(
        &painter, plot, composite,
        theme_tokens::PlotColor(theme_tokens::PlotColorToken::kCompositePrimary,
                                dark_theme_));
  } else if (trace_mode_ == TraceMode::kLuma) {
    PaintTrace(&painter, plot, y_mv_,
               theme_tokens::PlotColor(
                   theme_tokens::PlotColorToken::kLumaPrimary, dark_theme_));
  } else if (trace_mode_ == TraceMode::kChroma) {
    PaintTrace(&painter, plot, c_mv_,
               theme_tokens::PlotColor(
                   theme_tokens::PlotColorToken::kChromaPrimary, dark_theme_));
  } else {
    PaintTrace(&painter, plot, y_mv_,
               theme_tokens::PlotColor(
                   theme_tokens::PlotColorToken::kLumaPrimary, dark_theme_));
    PaintTrace(&painter, plot, c_mv_,
               theme_tokens::PlotColor(
                   theme_tokens::PlotColorToken::kChromaPrimary, dark_theme_));
  }

  // Cursor marker.
  const int sample_count = static_cast<int>(y_mv_.size());
  if (cursor_sample_ >= 0 && cursor_sample_ < sample_count) {
    const double x =
        plot.left() + SampleToPlotX(cursor_sample_, sample_count, plot.width());
    painter.setPen(
        QPen(theme_tokens::PlotColor(
                 theme_tokens::PlotColorToken::kMarkerSelection, dark_theme_),
             1.0, Qt::DashLine));
    painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
  }
}

void WaveformScopeWidget::mouseMoveEvent(QMouseEvent* event) {
  const QRect plot = PlotRect();
  const int sample_count = static_cast<int>(y_mv_.size());
  if (!plot.isValid() || sample_count < 2 || !plot.contains(event->pos())) {
    return;
  }

  cursor_sample_ =
      PlotXToSample(event->pos().x() - plot.left(), sample_count, plot.width());
  const double microseconds =
      SampleIndexToMicroseconds(cursor_sample_, sample_rate_hz_);
  const double millivolts =
      PlotYToMillivolts(event->pos().y() - plot.top(), range_, plot.height());
  emit CursorMoved(cursor_sample_, microseconds, millivolts);
  update();
}

void WaveformScopeWidget::leaveEvent(QEvent* event) {
  Q_UNUSED(event);
  cursor_sample_ = -1;
  update();
}

}  // namespace videosynth::gui
