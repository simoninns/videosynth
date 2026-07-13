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
}

void PictureViewWidget::mousePressEvent(QMouseEvent* event) {
  const QRect target = TargetRect();
  if (image_.isNull() || target.isEmpty() || !target.contains(event->pos())) {
    QWidget::mousePressEvent(event);
    return;
  }

  const double relative_y =
      static_cast<double>(event->pos().y() - target.top()) /
      static_cast<double>(target.height());
  const int row = std::clamp(static_cast<int>(relative_y * image_.height()), 0,
                             image_.height() - 1);
  emit RowClicked(row);
}

}  // namespace videosynth::gui
