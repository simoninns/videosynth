/*
 * File:        picture_view_widget.h
 * Module:      gui
 * Purpose:     Zoomable raster view for source and encoded preview images
 *              with picture-row click selection
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <QImage>
#include <QWidget>

namespace videosynth::gui {

// Paints a preview raster centred in the widget. Field rasters carry half
// the frame's vertical resolution, so an optional 2× vertical stretch keeps
// the displayed geometry frame-like. Fit mode scales to the widget while
// preserving the (stretched) aspect ratio; fixed zooms report a matching
// sizeHint so the view can live in a QScrollArea.
//
// Thread-safety: NOT thread-safe. GUI (main) thread only.
class PictureViewWidget : public QWidget {
  Q_OBJECT

 public:
  enum class ZoomMode {
    kFit,
    k100Percent,
    k200Percent,
  };

  explicit PictureViewWidget(QWidget* parent = nullptr);

  // Replaces the displayed image. `double_vertically` enables the 2× field
  // stretch (encoded field rasters); source frames pass false.
  void SetImage(const QImage& image, bool double_vertically);

  void SetZoomMode(ZoomMode mode);
  ZoomMode zoom_mode() const { return zoom_mode_; }

  // Enables the draggable line crosshair overlay. When on, clicking (and
  // dragging) inside the image places a crosshair and streams the picked row
  // through RowClicked; a horizontal guide marks the selected line.
  void SetCrosshairEnabled(bool enabled);

  // Positions the crosshair on `row` without emitting RowClicked, so the
  // overlay can follow an external line selection (e.g. the line spinbox).
  void SetCrosshairRow(int row);

  QSize sizeHint() const override;

  // Fixed zooms report the zoomed image size so a resizable QScrollArea
  // shows scrollbars; fit mode reports a small floor.
  QSize minimumSizeHint() const override;

 signals:
  // Emitted when the user clicks or drags inside the image; `row` is the
  // 0-based image row (frame line row for encoded rasters).
  void RowClicked(int row);

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;

 private:
  // Rectangle the image is painted into for the current zoom mode.
  QRect TargetRect() const;
  // Picks the 0-based image row/column under a widget-space point, clamped to
  // the image; returns false when there is no image to pick from.
  bool PickImagePoint(const QPoint& pos, int* row, int* column) const;

  QImage image_;
  bool double_vertically_ = false;
  ZoomMode zoom_mode_ = ZoomMode::kFit;
  bool crosshair_enabled_ = false;
  int crosshair_row_ = -1;
  int crosshair_column_ = -1;
};

}  // namespace videosynth::gui
