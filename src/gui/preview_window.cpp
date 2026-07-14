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
#include <QStatusBar>

namespace videosynth::gui {

namespace {

constexpr const char* kGeometrySettingsKey = "preview_window/geometry";

}  // namespace

PreviewWindow::PreviewWindow(ProjectDocument* document,
                             ThemeController* theme_controller, QWidget* parent)
    : QMainWindow(parent, Qt::Window) {
  setWindowTitle(tr("Preview — videosynth"));

  preview_pane_ = new PreviewPane(document, theme_controller, this);
  setCentralWidget(preview_pane_);

  // Create the status bar up front so it reserves its height immediately;
  // otherwise the first status message would lazily construct it and resize the
  // window. showMessage() below reuses this same bar.
  statusBar();

  // Surface the pane's stale/error status in the status bar instead of an
  // inline banner, so the preview content never shifts. A 0 timeout keeps the
  // message shown until the pane clears it (frame refreshed).
  connect(preview_pane_, &PreviewPane::StatusMessageChanged, this,
          [this](const QString& message) {
            if (message.isEmpty()) {
              statusBar()->clearMessage();
            } else {
              statusBar()->showMessage(message);
            }
          });

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
