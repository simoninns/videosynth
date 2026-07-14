/*
 * File:        preview_window.h
 * Module:      gui
 * Purpose:     Standalone top-level window hosting the signal preview
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <QCloseEvent>
#include <QMainWindow>

#include "preview_pane.h"
#include "project_document.h"
#include "theme_controller.h"

namespace videosynth::gui {

// Detached preview window. Hosts a PreviewPane as its central widget in an
// independent top-level window the user opens on demand (View > Preview), so it
// can sit alongside the main window (e.g. on a second monitor). Stale/error
// status from the pane is surfaced in the window's status bar rather than an
// inline banner, so the preview content never reflows. Geometry is persisted
// via QSettings. Closing the window hides it and emits VisibilityChanged(false)
// so the main window can keep its menu toggle in sync; it does not affect the
// project.
//
// Thread-safety: NOT thread-safe. GUI (main) thread only.
class PreviewWindow : public QMainWindow {
  Q_OBJECT

 public:
  // Does not take ownership of `document` or `theme_controller`; both must
  // outlive the window.
  PreviewWindow(ProjectDocument* document, ThemeController* theme_controller,
                QWidget* parent = nullptr);

  // Jumps the preview to the section's first output frame ("preview this
  // section").
  void ShowSectionFirstFrame(int section_index);

 signals:
  // Emitted when the window is shown or hidden so the View menu toggle stays
  // consistent with the actual window state.
  void VisibilityChanged(bool visible);

 protected:
  void showEvent(QShowEvent* event) override;
  void hideEvent(QHideEvent* event) override;
  void closeEvent(QCloseEvent* event) override;

 private:
  PreviewPane* preview_pane_ = nullptr;
};

}  // namespace videosynth::gui
