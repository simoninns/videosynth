/*
 * File:        preview_pane.h
 * Module:      gui
 * Purpose:     Dockable signal preview: frame navigator, source and encoded
 *              picture views, and a separately mountable line scope panel
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <QEvent>
#include <QObject>
#include <QShowEvent>
#include <QString>
#include <QTimer>
#include <QWidget>
#include <memory>

#include "picture_view_widget.h"
#include "preview_frame_service.h"
#include "project_document.h"
#include "theme_controller.h"
#include "waveform_scope_widget.h"

class QComboBox;
class QLabel;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QTabWidget;

namespace videosynth::gui {

// Signal preview surface. Frames are synthesised on demand through
// PreviewFrameService (the same deterministic backend the pipeline uses);
// switching the encoded Composite/Y/C mode re-renders from the cached Y/C
// buffers without re-running synthesis. The encoded view shows the full woven
// interlaced frame; clicking or dragging its crosshair selects the line shown
// in the waveform scope. Noise and dropouts are always applied so the preview
// matches the written output. Document edits trigger a debounced re-preview;
// while the refresh is pending or the project is invalid, StatusMessageChanged
// reports the stale/error state (surfaced by the host's status bar). A project
// edit re-synthesises from scratch, so the picture area is replaced by a
// "Loading…" placeholder until the fresh frame arrives (a slow new source asset
// never leaves a misleading stale frame on screen). Frame stepping keeps the
// current frame visible instead, so scrubbing the navigator never flickers.
//
// The pane splits into two host-mountable panels sharing one frame service and
// one selected frame: the pane itself (navigator plus picture views) and the
// line-scope panel returned by scope_panel(). The host docks them separately;
// showing either panel is enough to trigger a pending refresh.
//
// Thread-safety: NOT thread-safe. GUI (main) thread only.
class PreviewPane : public QWidget {
  Q_OBJECT

 public:
  // Does not take ownership of `document` or `theme_controller`; both must
  // outlive the pane.
  explicit PreviewPane(ProjectDocument* document,
                       ThemeController* theme_controller,
                       QWidget* parent = nullptr);

  // Debounce delay between the last document edit and the service refresh.
  void SetDebounceInterval(int msec);

  // The line-scope panel (line selector, trace mode, vertical range, readout,
  // and waveform).
  // Ownership follows the usual Qt parent rules: the panel starts parented to
  // the pane and the host takes it over when it mounts it (e.g. into a dock).
  QWidget* scope_panel() { return scope_panel_; }

  // Jumps the navigator to the section's first output frame ("preview this
  // section"); deferred until the schedule is known when necessary.
  void ShowSectionFirstFrame(int section_index);

 signals:
  // Reports the current preview status line: a non-empty message when the shown
  // frame is stale, updating, or a synthesis error occurred; an empty string
  // when the frame is current. The host window surfaces this in its status bar.
  void StatusMessageChanged(const QString& message);

  // Reports the project section the shown frame belongs to, so the host can
  // keep the section list in step as the navigator scrubs across section
  // boundaries. Emitted only when the section actually changes; -1 when the
  // frame belongs to no section.
  void CurrentSectionChanged(int section_index);

 protected:
  void showEvent(QShowEvent* event) override;
  // Watches the detached scope panel so it triggers a pending refresh when it
  // becomes visible, exactly as showEvent does for this pane.
  bool eventFilter(QObject* watched, QEvent* event) override;

 private slots:
  void OnDocumentChanged();
  void OnRefreshTimeout();
  void OnScheduleInfoChanged();
  void OnFrameReady(std::shared_ptr<const PreviewFrameData> frame);
  void OnPreviewFailed(quint64 revision, const QString& message);
  void OnFrameIndexChanged(int output_frame_index);
  void OnEncodedModeChanged();
  void OnEncodedRowClicked(int row);
  void OnScopeLineChanged(int line_number);
  void OnCursorMoved(int sample_index, double microseconds, double millivolts);

 private:
  void BuildUi();
  void BuildScopePanel();
  // True when either mounted panel is on screen; a hidden preview must not
  // spend synthesis work, but either panel alone is reason enough to refresh.
  bool IsAnyPanelVisible() const;
  void PushProjectToService();
  void RequestCurrentFrame();
  PreviewOptions CurrentOptions() const;
  void UpdatePictures();
  void UpdateScope();
  void UpdateFrameInfoLabel();
  void SetStatusMessage(const QString& message);
  // Swaps the picture area between the live views and the "Loading…"
  // placeholder while a synthesis request is in flight.
  void SetLoadingVisible(bool visible);

  ProjectDocument* document_;
  ThemeController* theme_controller_;
  PreviewFrameService* service_;
  QTimer refresh_timer_;
  bool needs_refresh_ = true;
  int pending_section_jump_ = -1;
  // Last section reported through CurrentSectionChanged; -2 means "nothing
  // reported yet" so the first frame always emits.
  int reported_section_index_ = -2;

  std::shared_ptr<const PreviewFrameData> current_frame_;
  QString status_message_;

  QSlider* frame_slider_ = nullptr;
  QSpinBox* frame_spinbox_ = nullptr;
  QLabel* frame_total_label_ = nullptr;
  QStackedWidget* view_stack_ = nullptr;
  QLabel* loading_placeholder_ = nullptr;
  QTabWidget* view_tabs_ = nullptr;
  PictureViewWidget* source_view_ = nullptr;
  QLabel* source_placeholder_ = nullptr;
  QComboBox* encoded_mode_combo_ = nullptr;
  QComboBox* zoom_combo_ = nullptr;
  PictureViewWidget* encoded_view_ = nullptr;
  QLabel* frame_info_label_ = nullptr;
  QWidget* scope_panel_ = nullptr;
  QSpinBox* line_spinbox_ = nullptr;
  QComboBox* trace_combo_ = nullptr;
  QComboBox* range_combo_ = nullptr;
  QLabel* readout_label_ = nullptr;
  WaveformScopeWidget* scope_ = nullptr;
};

}  // namespace videosynth::gui
