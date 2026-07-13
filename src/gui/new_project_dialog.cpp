/*
 * File:        new_project_dialog.cpp
 * Module:      gui
 * Purpose:     Modal dialog that walks the user through creating a project
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "new_project_dialog.h"

#include <QDialogButtonBox>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "project_templates.h"

namespace videosynth::gui {

NewProjectDialog::NewProjectDialog(QWidget* parent)
    : QDialog(parent), scratch_(new ProjectDocument(this)) {
  setWindowTitle(tr("New Project"));
  setModal(true);

  // Seed the scratch document with a minimal valid PAL project; the standard
  // control lets the user switch it before creating.
  scratch_->ResetProject(MakeDefaultProject(Standard::kPal), QString());

  auto* layout = new QVBoxLayout(this);

  auto* scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  settings_editor_ = new ProjectSettingsEditor(scratch_, scroll);
  scroll->setWidget(settings_editor_);
  layout->addWidget(scroll);

  auto* buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  buttons->button(QDialogButtonBox::Ok)->setText(tr("Create"));
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);

  resize(560, 640);
}

}  // namespace videosynth::gui
