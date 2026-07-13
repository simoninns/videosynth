/*
 * File:        project_settings_editor.h
 * Module:      gui
 * Purpose:     Form editor for project info, CVBS presets, and output targets
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <QWidget>

#include "project_document.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;

namespace videosynth::gui {

// Editor for the project-level YAML blocks (project:, cvbs_presets:,
// output:). Every control maps 1:1 to a Project field and commits through
// ProjectDocument commands; standard-dependent enablement follows
// BuildProjectSettingsFormState so invalid combinations cannot be entered.
//
// Thread-safety: NOT thread-safe. GUI (main) thread only.
class ProjectSettingsEditor : public QWidget {
  Q_OBJECT

 public:
  // Does not take ownership of `document`; it must outlive the editor.
  explicit ProjectSettingsEditor(ProjectDocument* document,
                                 QWidget* parent = nullptr);

  // Locks or unlocks the video-standard control. The standard is chosen once
  // when a project is created (it drives raster/timing); the Edit Project
  // dialog calls this with `false` so it is shown read-only.
  void SetStandardEditable(bool editable);

 private:
  void BuildUi();
  void LoadFromDocument();
  void ApplyEnablement();

  void CommitProjectInfo();
  void CommitCvbsPresets();
  void CommitOutputTargets();

  void OnStandardChanged();
  void OnSignalTypeChanged();
  void OnBrowseVideoPath();
  void OnDeriveMetadataPath();

  ProjectDocument* document_;
  bool updating_ = false;
  bool committing_ = false;
  bool standard_editable_ = true;

  QLineEdit* name_edit_ = nullptr;
  QLineEdit* version_edit_ = nullptr;
  QPlainTextEdit* description_edit_ = nullptr;

  QComboBox* standard_combo_ = nullptr;
  QComboBox* sample_encoding_combo_ = nullptr;
  QComboBox* signal_state_combo_ = nullptr;
  QCheckBox* pilot_burst_check_ = nullptr;
  QCheckBox* vbi_burst_check_ = nullptr;
  QCheckBox* setup_ire_check_ = nullptr;
  QComboBox* setup_ire_combo_ = nullptr;

  QComboBox* signal_type_combo_ = nullptr;
  QLineEdit* video_path_edit_ = nullptr;
  QLineEdit* metadata_path_edit_ = nullptr;
  QLabel* video_path_hint_ = nullptr;
};

}  // namespace videosynth::gui
