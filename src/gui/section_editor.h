/*
 * File:        section_editor.h
 * Module:      gui
 * Purpose:     Per-section form editor: source and span, optional audio,
 *              noise, dropout, OSD blocks, and line injections
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <QWidget>

#include "line_injections_editor.h"
#include "project_document.h"
#include "source_probe_controller.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace videosynth::gui {

// Editor for one section of the project. Widgets stage edits into a copy of
// the section and commit through ProjectDocument::SetSection; optional YAML
// blocks are toggled through the section_block_presenters helpers so
// disabled blocks never emit defaults. Source probing runs on the
// SourceProbeController worker and never blocks the UI.
//
// Thread-safety: NOT thread-safe. GUI (main) thread only.
class SectionEditor : public QWidget {
  Q_OBJECT

 public:
  // Does not take ownership of `document` or `probe_controller`; both must
  // outlive the editor.
  SectionEditor(ProjectDocument* document,
                SourceProbeController* probe_controller,
                QWidget* parent = nullptr);

  // Selects the section to edit; -1 shows the disabled placeholder state.
  void SetCurrentSection(int index);
  int current_section() const { return section_index_; }

 private:
  void BuildUi();
  QWidget* BuildGeneralGroup();
  QWidget* BuildProbeGroup();
  QWidget* BuildAudioGroup();
  QWidget* BuildNoiseGroup();
  QWidget* BuildDropoutsGroup();
  QWidget* BuildOsdGroup();
  QWidget* BuildInjectionsGroup();

  void LoadFromDocument();
  void LoadOsdTable(const Section& section);
  // Reads the staged widget state into a Section and commits it.
  void CommitSection();
  Section SectionFromWidgets() const;
  void RequestProbe();
  void UpdateProbeDisplay();

  void OnBrowseSource();
  void OnAddOverlay();
  void OnRemoveOverlay();

  ProjectDocument* document_;
  SourceProbeController* probe_controller_;
  int section_index_ = -1;
  bool updating_ = false;
  bool committing_ = false;

  QWidget* content_ = nullptr;
  QLabel* placeholder_ = nullptr;

  // General.
  QLineEdit* name_edit_ = nullptr;
  QComboBox* section_type_combo_ = nullptr;
  QLineEdit* source_edit_ = nullptr;
  QSpinBox* duration_spin_ = nullptr;
  QCheckBox* duration_all_check_ = nullptr;
  QLabel* start_frame_label_ = nullptr;

  // Probe.
  QLabel* probe_status_label_ = nullptr;
  QLabel* probe_detail_label_ = nullptr;

  // Audio.
  QGroupBox* audio_group_ = nullptr;
  QComboBox* waveform_combo_ = nullptr;
  QDoubleSpinBox* frequency_spin_ = nullptr;
  QDoubleSpinBox* amplitude_spin_ = nullptr;
  QGroupBox* ramp_group_ = nullptr;
  QDoubleSpinBox* ramp_start_spin_ = nullptr;
  QDoubleSpinBox* ramp_end_spin_ = nullptr;
  QComboBox* ramp_mode_combo_ = nullptr;
  QDoubleSpinBox* ramp_period_spin_ = nullptr;

  // Noise.
  QGroupBox* noise_group_ = nullptr;
  QDoubleSpinBox* noise_db_spin_ = nullptr;
  QDoubleSpinBox* noise_spread_spin_ = nullptr;
  QCheckBox* noise_seed_check_ = nullptr;
  QLineEdit* noise_seed_edit_ = nullptr;

  // Dropouts.
  QGroupBox* random_dropouts_group_ = nullptr;
  QSpinBox* random_scale_spin_ = nullptr;
  QCheckBox* random_seed_check_ = nullptr;
  QLineEdit* random_seed_edit_ = nullptr;
  QGroupBox* scratch_dropouts_group_ = nullptr;
  QSpinBox* scratch_scale_spin_ = nullptr;
  QCheckBox* scratch_seed_check_ = nullptr;
  QLineEdit* scratch_seed_edit_ = nullptr;

  // OSD.
  QGroupBox* osd_group_ = nullptr;
  QTableWidget* osd_table_ = nullptr;
  QPushButton* remove_overlay_button_ = nullptr;

  // Line injections.
  LineInjectionsEditor* injections_editor_ = nullptr;
};

}  // namespace videosynth::gui
