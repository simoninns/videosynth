/*
 * File:        preview_pane.h
 * Module:      gui
 * Purpose:     Central preview tab: frame/field navigator, source and
 *              encoded picture views, and the line waveform scope
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <QShowEvent>
#include <QTimer>
#include <QWidget>
#include <memory>

#include "picture_view_widget.h"
#include "preview_frame_service.h"
#include "project_document.h"
#include "theme_controller.h"
#include "waveform_scope_widget.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QSlider;
class QSpinBox;
class QSplitter;
class QTabWidget;

namespace videosynth::gui {

// Central preview area. Frames are synthesised on demand through
// PreviewFrameService (the same deterministic backend the pipeline uses);
// switching composite/Y/C modes and fields re-renders from the cached Y/C
// buffers without re-running synthesis. Document edits trigger a debounced
// re-preview; while the refresh is pending or the project is invalid, a
// banner marks the shown frame as stale and the last good frame stays
// visible.
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

  // Jumps the navigator to the section's first output frame ("preview this
  // section"); deferred until the schedule is known when necessary.
  void ShowSectionFirstFrame(int section_index);

 protected:
  void showEvent(QShowEvent* event) override;

 private slots:
  void OnDocumentChanged();
  void OnRefreshTimeout();
  void OnScheduleInfoChanged();
  void OnFrameReady(std::shared_ptr<const PreviewFrameData> frame);
  void OnPreviewFailed(quint64 revision, const QString& message);
  void OnFrameIndexChanged(int output_frame_index);
  void OnFieldOrModeChanged();
  void OnDegradationToggled();
  void OnEncodedRowClicked(int row);
  void OnScopeLineChanged(int line_number);
  void OnCursorMoved(int sample_index, double microseconds, double millivolts);

 private:
  void BuildUi();
  void PushProjectToService();
  void RequestCurrentFrame();
  PreviewOptions CurrentOptions() const;
  void UpdatePictures();
  void UpdateScope();
  void UpdateFrameInfoLabel();
  void ShowBanner(const QString& message);

  ProjectDocument* document_;
  ThemeController* theme_controller_;
  PreviewFrameService* service_;
  QTimer refresh_timer_;
  bool needs_refresh_ = true;
  int pending_section_jump_ = -1;

  std::shared_ptr<const PreviewFrameData> current_frame_;

  QLabel* banner_label_ = nullptr;
  QSlider* frame_slider_ = nullptr;
  QSpinBox* frame_spinbox_ = nullptr;
  QLabel* frame_total_label_ = nullptr;
  QComboBox* field_combo_ = nullptr;
  QCheckBox* noise_checkbox_ = nullptr;
  QCheckBox* dropouts_checkbox_ = nullptr;
  QTabWidget* view_tabs_ = nullptr;
  PictureViewWidget* source_view_ = nullptr;
  QLabel* source_placeholder_ = nullptr;
  QComboBox* encoded_mode_combo_ = nullptr;
  QComboBox* zoom_combo_ = nullptr;
  QSplitter* encoded_splitter_ = nullptr;
  PictureViewWidget* encoded_primary_view_ = nullptr;
  PictureViewWidget* encoded_secondary_view_ = nullptr;
  QLabel* frame_info_label_ = nullptr;
  QSpinBox* line_spinbox_ = nullptr;
  QComboBox* trace_combo_ = nullptr;
  QLabel* readout_label_ = nullptr;
  WaveformScopeWidget* scope_ = nullptr;
  // Last project signal_type applied as the encoded default, so a user's
  // manual composite/Y/C override survives unrelated document edits.
  std::string applied_signal_type_;
};

}  // namespace videosynth::gui
