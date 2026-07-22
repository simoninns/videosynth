/*
 * File:        line_injections_editor.h
 * Module:      gui
 * Purpose:     Editor for a section's laserdisc biphase code injections
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <QWidget>
#include <string>
#include <vector>

#include "videosynth/biphase_types.h"
#include "videosynth/model.h"

class QCheckBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QToolButton;

namespace videosynth::gui {

// Edits a working copy of a section's laserdisc code injections. Section-level
// injections are laserdisc-only and the runtime uses a single injection per
// section, so this editor presents one implicit injection as a *checklist* of
// the code types valid for the current disc format / section type / standard:
// tick a code to include it, with an optional value beside codes that carry
// one. There is no add/remove flow — the offered codes are fixed by the
// compatibility matrix, and the "expected" set for the section type is
// pre-ticked (see line_injection_presenter's Recommended/Available catalogues,
// which mirror the validator).
//
// The disc_type (CAV/CLV) and the VITS set are project-wide and edited in the
// project settings; this editor takes the resolved disc_type through
// SetContext. The owner reads back injections() and commits after every
// InjectionsEdited signal.
//
// Thread-safety: NOT thread-safe. GUI (main) thread only.
class LineInjectionsEditor : public QWidget {
  Q_OBJECT

 public:
  explicit LineInjectionsEditor(QWidget* parent = nullptr);

  // Catalogue context: the project standard, the owning section's type, and
  // the project-wide disc format. Refreshes the offered choices without
  // touching the working copy (beyond pruning codes no longer valid for the
  // context, so the checklist and the model stay in step).
  void SetContext(Standard standard, SectionType section_type,
                  DiscType disc_type);

  // Replaces the working copy (no InjectionsEdited emission). Any legacy list
  // of injections is collapsed to a single laserdisc injection.
  void SetInjections(std::vector<Section::LineInjection> injections);

  const std::vector<Section::LineInjection>& injections() const {
    return injections_;
  }

  // Seeds a single laserdisc injection pre-populated with the recommended
  // ("expected") codes for the current section type and emits InjectionsEdited.
  // The section editor calls this when its "include" checkbox is switched on so
  // the block starts with the codes a section of that type normally carries.
  void AddDefaultInjection();

 signals:
  // The working copy changed through user interaction.
  void InjectionsEdited();

 private:
  // One checklist row: a code type, its tick box, and (for codes that carry a
  // parameter) the value editor beside it. `value` is null for value-less
  // codes such as lead_in/lead_out/clv_code. `configure` is the flag-picker
  // button beside the programme_status hex field, null for other codes.
  struct CodeRow {
    std::string code_type;
    QCheckBox* check = nullptr;
    QLineEdit* value = nullptr;
    QToolButton* configure = nullptr;
  };

  void LoadInjectionForm();
  void RebuildCodeChecklist();
  void ClearChecklist();
  void AnnounceEdit();

  // The single implicit laserdisc injection, or nullptr when the block holds
  // none (block excluded).
  Section::LineInjection* CurrentInjection();

  // Rebuilds injection->codes from the current tick/value state (canonical
  // order) and emits InjectionsEdited when it changed.
  void OnChecklistChanged();

  // Opens the programme-status flag picker seeded from `value_edit` and, on
  // accept, writes the composed hex word back and commits the change.
  void OnConfigureProgrammeStatus(QLineEdit* value_edit);

  Standard standard_ = Standard::kUnknown;
  SectionType section_type_ = SectionType::kUnknown;
  DiscType disc_type_ = DiscType::kUnknown;
  std::vector<Section::LineInjection> injections_;
  bool updating_ = false;

  QLabel* disc_type_label_ = nullptr;
  QLabel* empty_hint_label_ = nullptr;
  QGridLayout* checklist_layout_ = nullptr;
  std::vector<CodeRow> code_rows_;
};

}  // namespace videosynth::gui
