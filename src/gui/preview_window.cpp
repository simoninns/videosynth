/*
 * File:        preview_window.cpp
 * Module:      gui
 * Purpose:     Standalone top-level window hosting the signal preview
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "preview_window.h"

#include <QSettings>
#include <QVBoxLayout>

namespace videosynth::gui {

namespace {

constexpr const char* kGeometrySettingsKey = "preview_window/geometry";

}  // namespace

PreviewWindow::PreviewWindow(ProjectDocument* document,
                             ThemeController* theme_controller, QWidget* parent)
    : QWidget(parent, Qt::Window) {
  setWindowTitle(tr("Preview — videosynth"));

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  preview_pane_ = new PreviewPane(document, theme_controller, this);
  layout->addWidget(preview_pane_);

  const QSettings settings;
  const QByteArray geometry =
      settings.value(QLatin1String(kGeometrySettingsKey)).toByteArray();
  if (!geometry.isEmpty()) {
    restoreGeometry(geometry);
  } else {
    resize(900, 640);
  }
}

void PreviewWindow::ShowSectionFirstFrame(int section_index) {
  preview_pane_->ShowSectionFirstFrame(section_index);
}

void PreviewWindow::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  emit VisibilityChanged(true);
}

void PreviewWindow::hideEvent(QHideEvent* event) {
  QWidget::hideEvent(event);
  emit VisibilityChanged(false);
}

void PreviewWindow::closeEvent(QCloseEvent* event) {
  QSettings settings;
  settings.setValue(QLatin1String(kGeometrySettingsKey), saveGeometry());
  // Hide rather than destroy so re-opening keeps preview state and geometry.
  hide();
  event->ignore();
}

}  // namespace videosynth::gui
