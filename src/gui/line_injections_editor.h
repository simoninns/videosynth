/*
 * File:        line_injections_editor.h
 * Module:      gui
 * Purpose:     Editor for a section's line injections (VITS and laserdisc
 *              biphase codes)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <QWidget>
#include <vector>

#include "videosynth/biphase_types.h"
#include "videosynth/model.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;

namespace videosynth::gui {

// Edits a working copy of a section's line_injections list. Choices are
// constrained by the validator's compatibility matrix through the
// line_injection_presenter catalogues (standard-filtered vits types,
// disc/section/standard-filtered code types). The owner reads back
// injections() and commits after every InjectionsEdited signal.
//
// Thread-safety: NOT thread-safe. GUI (main) thread only.
class LineInjectionsEditor : public QWidget {
  Q_OBJECT

 public:
  explicit LineInjectionsEditor(QWidget* parent = nullptr);

  // Catalogue context: the project standard and the owning section's type.
  // Refreshes the offered choices without touching the working copy.
  void SetContext(Standard standard, SectionType section_type);

  // Replaces the working copy (no InjectionsEdited emission).
  void SetInjections(std::vector<Section::LineInjection> injections);

  const std::vector<Section::LineInjection>& injections() const {
    return injections_;
  }

  // Appends a validator-clean default injection (identical to the Add button)
  // and emits InjectionsEdited. The section editor uses this to seed the block
  // when its "include" checkbox is switched on so the block is never empty.
  void AddDefaultInjection();

 signals:
  // The working copy changed through user interaction.
  void InjectionsEdited();

 private:
  void RebuildInjectionList(int select_row);
  void LoadInjectionForm();
  void RebuildCodesTable(const Section::LineInjection& injection);
  void AnnounceEdit();

  Section::LineInjection* CurrentInjection();

  void OnAddInjection();
  void OnRemoveInjection();
  void OnTypeChanged();
  void OnVitsTypeChanged();
  void OnTargetLinesEdited();
  void OnDiscTypeChanged();
  void OnAddCode();
  void OnRemoveCode();
  void OnCodeTypeChanged(int row);
  void OnCodeValueEdited(int row);

  QString InjectionSummary(const Section::LineInjection& injection) const;

  Standard standard_ = Standard::kUnknown;
  SectionType section_type_ = SectionType::kUnknown;
  std::vector<Section::LineInjection> injections_;
  bool updating_ = false;

  QListWidget* injection_list_ = nullptr;
  QPushButton* remove_injection_button_ = nullptr;

  QWidget* form_panel_ = nullptr;
  QComboBox* type_combo_ = nullptr;

  QWidget* vits_panel_ = nullptr;
  QComboBox* vits_type_combo_ = nullptr;
  QLineEdit* target_lines_edit_ = nullptr;
  QLabel* vits_line_hint_ = nullptr;

  QWidget* laserdisc_panel_ = nullptr;
  QComboBox* disc_type_combo_ = nullptr;
  QTableWidget* codes_table_ = nullptr;
  QPushButton* remove_code_button_ = nullptr;
};

}  // namespace videosynth::gui
