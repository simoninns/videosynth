/*
 * File:        picture_view_widget.cpp
 * Module:      gui
 * Purpose:     Zoomable raster view for source and encoded preview images
 *              with picture-row click selection
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "picture_view_widget.h"

#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <algorithm>

namespace videosynth::gui {

namespace {

constexpr int kMinimumSideHint = 160;

}  // namespace

PictureViewWidget::PictureViewWidget(QWidget* parent) : QWidget(parent) {
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void PictureViewWidget::SetImage(const QImage& image, bool double_vertically) {
  image_ = image;
  double_vertically_ = double_vertically;
  updateGeometry();
  update();
}

void PictureViewWidget::SetZoomMode(ZoomMode mode) {
  if (zoom_mode_ == mode) {
    return;
  }
  zoom_mode_ = mode;
  updateGeometry();
  update();
}

void PictureViewWidget::SetCrosshairEnabled(bool enabled) {
  if (crosshair_enabled_ == enabled) {
    return;
  }
  crosshair_enabled_ = enabled;
  update();
}

void PictureViewWidget::SetCrosshairRow(int row) {
  if (image_.isNull()) {
    return;
  }
  const int clamped = std::clamp(row, 0, image_.height() - 1);
  if (crosshair_row_ == clamped) {
    return;
  }
  crosshair_row_ = clamped;
  update();
}

QSize PictureViewWidget::sizeHint() const {
  if (image_.isNull()) {
    return QSize(kMinimumSideHint, kMinimumSideHint / 2);
  }
  const int stretch = double_vertically_ ? 2 : 1;
  const int zoom = (zoom_mode_ == ZoomMode::k200Percent) ? 2 : 1;
  return QSize(image_.width() * zoom, image_.height() * stretch * zoom);
}

QSize PictureViewWidget::minimumSizeHint() const {
  if (zoom_mode_ == ZoomMode::kFit || image_.isNull()) {
    return QSize(kMinimumSideHint, kMinimumSideHint / 2);
  }
  return sizeHint();
}

QRect PictureViewWidget::TargetRect() const {
  if (image_.isNull()) {
    return QRect();
  }

  const int stretch = double_vertically_ ? 2 : 1;
  const QSize natural(image_.width(), image_.height() * stretch);

  if (zoom_mode_ != ZoomMode::kFit) {
    const int zoom = (zoom_mode_ == ZoomMode::k200Percent) ? 2 : 1;
    const QSize zoomed = natural * zoom;
    const QPoint origin(std::max(0, (width() - zoomed.width()) / 2),
                        std::max(0, (height() - zoomed.height()) / 2));
    return QRect(origin, zoomed);
  }

  QSize scaled = natural;
  scaled.scale(size(), Qt::KeepAspectRatio);
  if (scaled.isEmpty()) {
    return QRect();
  }
  const QPoint origin((width() - scaled.width()) / 2,
                      (height() - scaled.height()) / 2);
  return QRect(origin, scaled);
}

void PictureViewWidget::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.fillRect(rect(), palette().color(QPalette::Base));

  const QRect target = TargetRect();
  if (image_.isNull() || target.isEmpty()) {
    return;
  }
  // Nearest-neighbour scaling keeps sample and line boundaries crisp, which
  // matters more than smoothing for signal inspection.
  painter.drawImage(target, image_);

  if (!crosshair_enabled_ || crosshair_row_ < 0) {
    return;
  }
  // The crosshair marks the selected line (horizontal) and the picked sample
  // column (vertical) in high-contrast so it reads over any picture content.
  const double row_fraction =
      (crosshair_row_ + 0.5) / static_cast<double>(image_.height());
  const int y = target.top() + static_cast<int>(row_fraction * target.height());
  painter.setPen(QPen(QColor(0, 200, 255), 1.0));
  painter.drawLine(target.left(), y, target.right(), y);
  if (crosshair_column_ >= 0) {
    const double col_fraction =
        (crosshair_column_ + 0.5) / static_cast<double>(image_.width());
    const int x =
        target.left() + static_cast<int>(col_fraction * target.width());
    painter.drawLine(x, target.top(), x, target.bottom());
  }
}

bool PictureViewWidget::PickImagePoint(const QPoint& pos, int* row,
                                       int* column) const {
  const QRect target = TargetRect();
  if (image_.isNull() || target.isEmpty()) {
    return false;
  }
  const double relative_y = static_cast<double>(pos.y() - target.top()) /
                            static_cast<double>(target.height());
  const double relative_x = static_cast<double>(pos.x() - target.left()) /
                            static_cast<double>(target.width());
  *row = std::clamp(static_cast<int>(relative_y * image_.height()), 0,
                    image_.height() - 1);
  *column = std::clamp(static_cast<int>(relative_x * image_.width()), 0,
                       image_.width() - 1);
  return true;
}

void PictureViewWidget::mousePressEvent(QMouseEvent* event) {
  int row = 0;
  int column = 0;
  if (!PickImagePoint(event->pos(), &row, &column)) {
    QWidget::mousePressEvent(event);
    return;
  }
  if (crosshair_enabled_) {
    crosshair_row_ = row;
    crosshair_column_ = column;
    update();
  }
  emit RowClicked(row);
}

void PictureViewWidget::mouseMoveEvent(QMouseEvent* event) {
  if (!crosshair_enabled_ || (event->buttons() & Qt::LeftButton) == 0) {
    QWidget::mouseMoveEvent(event);
    return;
  }
  int row = 0;
  int column = 0;
  if (!PickImagePoint(event->pos(), &row, &column)) {
    return;
  }
  crosshair_row_ = row;
  crosshair_column_ = column;
  update();
  emit RowClicked(row);
}

}  // namespace videosynth::gui
