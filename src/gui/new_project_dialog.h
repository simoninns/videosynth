/*
 * File:        new_project_dialog.h
 * Module:      gui
 * Purpose:     Modal dialog that walks the user through creating a project
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <QDialog>

#include "project_document.h"
#include "project_settings_editor.h"
#include "videosynth/model.h"

namespace videosynth::gui {

// New Project dialog. Presents the full project-settings form (video standard,
// name, description, sample encoding, signal type, output targets, bursts) so
// the user flows through project creation in one place. Edits are staged into
// a private scratch ProjectDocument; on OK the caller reads project() and
// hands it to the real document. The video standard is editable here and is
// locked afterwards (the Edit Project dialog shows it read-only).
//
// Thread-safety: NOT thread-safe. GUI (main) thread only.
class NewProjectDialog : public QDialog {
  Q_OBJECT

 public:
  explicit NewProjectDialog(QWidget* parent = nullptr);

  // The project as configured by the user; only meaningful after the dialog
  // is accepted.
  Project project() const { return scratch_->project(); }

 private:
  ProjectDocument* scratch_;
  ProjectSettingsEditor* settings_editor_ = nullptr;
};

}  // namespace videosynth::gui
