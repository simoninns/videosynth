/*
 * File:        project_settings_editor.cpp
 * Module:      gui
 * Purpose:     Form editor for project info, CVBS presets, and output targets
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "project_settings_editor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "project_settings_presenter.h"

namespace videosynth::gui {

ProjectSettingsEditor::ProjectSettingsEditor(ProjectDocument* document,
                                             QWidget* parent)
    : QWidget(parent), document_(document) {
  BuildUi();
  LoadFromDocument();

  connect(document_, &ProjectDocument::DocumentReset, this,
          [this] { LoadFromDocument(); });
  connect(document_, &ProjectDocument::ProjectSettingsChanged, this, [this] {
    if (!committing_) {
      LoadFromDocument();
    }
  });
}

void ProjectSettingsEditor::BuildUi() {
  auto* layout = new QVBoxLayout(this);

  // --- project: ------------------------------------------------------------
  auto* project_group = new QGroupBox(tr("Project"), this);
  auto* project_form = new QFormLayout(project_group);
  name_edit_ = new QLineEdit(project_group);
  version_edit_ = new QLineEdit(project_group);
  description_edit_ = new QPlainTextEdit(project_group);
  description_edit_->setFixedHeight(60);
  description_edit_->setTabChangesFocus(true);
  project_form->addRow(tr("Name:"), name_edit_);
  project_form->addRow(tr("Version:"), version_edit_);
  project_form->addRow(tr("Description:"), description_edit_);
  layout->addWidget(project_group);

  connect(name_edit_, &QLineEdit::editingFinished, this,
          &ProjectSettingsEditor::CommitProjectInfo);
  connect(version_edit_, &QLineEdit::editingFinished, this,
          &ProjectSettingsEditor::CommitProjectInfo);
  // QPlainTextEdit has no editingFinished; commit on every change (the
  // document command layer collapses no-op commits).
  connect(description_edit_, &QPlainTextEdit::textChanged, this, [this] {
    if (!updating_) {
      CommitProjectInfo();
    }
  });

  // --- cvbs_presets: -------------------------------------------------------
  auto* presets_group = new QGroupBox(tr("CVBS Presets"), this);
  auto* presets_form = new QFormLayout(presets_group);
  standard_combo_ = new QComboBox(presets_group);
  sample_encoding_combo_ = new QComboBox(presets_group);
  signal_state_combo_ = new QComboBox(presets_group);
  pilot_burst_check_ = new QCheckBox(
      tr("PAL laserdisc pilot burst (IEC 60856 §9.1.2)"), presets_group);
  vbi_burst_check_ = new QCheckBox(
      tr("NTSC laserdisc VBI burst (IEC 60857 §9.1.2)"), presets_group);
  setup_ire_check_ =
      new QCheckBox(tr("Override NTSC black setup IRE"), presets_group);
  setup_ire_combo_ = new QComboBox(presets_group);

  presets_form->addRow(tr("Video standard:"), standard_combo_);
  presets_form->addRow(tr("Sample encoding:"), sample_encoding_combo_);
  presets_form->addRow(tr("Signal state:"), signal_state_combo_);
  presets_form->addRow(pilot_burst_check_);
  presets_form->addRow(vbi_burst_check_);
  auto* setup_row = new QHBoxLayout();
  setup_row->addWidget(setup_ire_check_);
  setup_row->addWidget(setup_ire_combo_);
  setup_row->addStretch();
  presets_form->addRow(setup_row);
  layout->addWidget(presets_group);

  connect(standard_combo_, &QComboBox::activated, this,
          &ProjectSettingsEditor::OnStandardChanged);
  connect(sample_encoding_combo_, &QComboBox::activated, this,
          &ProjectSettingsEditor::CommitCvbsPresets);
  connect(signal_state_combo_, &QComboBox::activated, this,
          &ProjectSettingsEditor::CommitCvbsPresets);
  connect(pilot_burst_check_, &QCheckBox::toggled, this, [this] {
    if (!updating_) {
      CommitCvbsPresets();
    }
  });
  connect(vbi_burst_check_, &QCheckBox::toggled, this, [this] {
    if (!updating_) {
      CommitCvbsPresets();
    }
  });
  connect(setup_ire_check_, &QCheckBox::toggled, this, [this](bool checked) {
    setup_ire_combo_->setEnabled(checked && setup_ire_check_->isEnabled());
    if (!updating_) {
      CommitCvbsPresets();
    }
  });
  connect(setup_ire_combo_, &QComboBox::activated, this,
          &ProjectSettingsEditor::CommitCvbsPresets);

  // --- output: -------------------------------------------------------------
  auto* output_group = new QGroupBox(tr("Output"), this);
  auto* output_form = new QFormLayout(output_group);
  signal_type_combo_ = new QComboBox(output_group);

  video_path_edit_ = new QLineEdit(output_group);
  auto* browse_button = new QPushButton(tr("Browse…"), output_group);
  auto* video_row = new QHBoxLayout();
  video_row->addWidget(video_path_edit_);
  video_row->addWidget(browse_button);

  // The metadata path is not independently settable: metadata, audio, and any
  // sidecar files are always written to the same folder as the video output,
  // sharing its base name. It is therefore not presented as a separate field;
  // the note below documents where those files are written.
  video_path_hint_ = new QLabel(output_group);
  video_path_hint_->setWordWrap(true);

  outputs_note_ = new QLabel(
      tr("Metadata (.meta), audio (.wav), and sidecar files are written to the "
         "same folder as the video output, sharing its base name."),
      output_group);
  outputs_note_->setWordWrap(true);

  output_form->addRow(tr("Signal type:"), signal_type_combo_);
  output_form->addRow(tr("Video path:"), video_row);
  output_form->addRow(QString(), video_path_hint_);
  output_form->addRow(QString(), outputs_note_);
  layout->addWidget(output_group);

  layout->addStretch();

  connect(signal_type_combo_, &QComboBox::activated, this,
          &ProjectSettingsEditor::OnSignalTypeChanged);
  connect(video_path_edit_, &QLineEdit::editingFinished, this,
          &ProjectSettingsEditor::CommitOutputTargets);
  connect(browse_button, &QPushButton::clicked, this,
          &ProjectSettingsEditor::OnBrowseVideoPath);
}

void ProjectSettingsEditor::LoadFromDocument() {
  updating_ = true;
  const Project& project = document_->project();
  const ProjectSettingsFormState state = BuildProjectSettingsFormState(project);

  name_edit_->setText(QString::fromStdString(project.name));
  version_edit_->setText(QString::fromStdString(project.version));
  if (description_edit_->toPlainText().toStdString() != project.description) {
    description_edit_->setPlainText(
        QString::fromStdString(project.description));
  }

  const auto fill_combo = [](QComboBox* combo,
                             const std::vector<std::string>& options,
                             const QString& current) {
    combo->clear();
    for (const std::string& option : options) {
      combo->addItem(QString::fromStdString(option));
    }
    const int index = combo->findText(current);
    if (index >= 0) {
      combo->setCurrentIndex(index);
    } else if (!current.isEmpty()) {
      // Preserve values the catalogue does not know (kept for round-trips).
      combo->addItem(current);
      combo->setCurrentIndex(combo->count() - 1);
    }
  };

  fill_combo(standard_combo_, state.standard_options,
             QString::fromStdString(
                 StandardToString(project.cvbs_presets.video_standard_preset)));
  fill_combo(
      sample_encoding_combo_, state.sample_encoding_options,
      QString::fromStdString(project.cvbs_presets.sample_encoding_preset));
  fill_combo(signal_state_combo_, state.signal_state_options,
             QString::fromStdString(project.cvbs_presets.signal_state_preset));
  fill_combo(signal_type_combo_, state.signal_type_options,
             QString::fromStdString(project.output.signal_type));

  setup_ire_combo_->clear();
  for (const double option : state.ntsc_black_setup_ire_options) {
    setup_ire_combo_->addItem(QString::number(option), option);
  }
  const int ire_index =
      setup_ire_combo_->findData(project.cvbs_presets.ntsc_black_setup_ire);
  setup_ire_combo_->setCurrentIndex(ire_index >= 0 ? ire_index : 0);

  pilot_burst_check_->setChecked(
      project.cvbs_presets.pal_laserdisc_pilot_burst);
  vbi_burst_check_->setChecked(project.cvbs_presets.ntsc_laserdisc_vbi_burst);
  setup_ire_check_->setChecked(
      project.cvbs_presets.ntsc_black_setup_ire_specified);

  video_path_edit_->setText(QString::fromStdString(project.output.video_path));

  ApplyEnablement();
  updating_ = false;
}

void ProjectSettingsEditor::SetStandardEditable(bool editable) {
  standard_editable_ = editable;
  standard_combo_->setEnabled(editable);
}

void ProjectSettingsEditor::ApplyEnablement() {
  const ProjectSettingsFormState state =
      BuildProjectSettingsFormState(document_->project());
  standard_combo_->setEnabled(standard_editable_);
  pilot_burst_check_->setEnabled(state.pilot_burst_editable);
  vbi_burst_check_->setEnabled(state.vbi_burst_editable);
  setup_ire_check_->setEnabled(state.setup_ire_editable);
  setup_ire_combo_->setEnabled(state.setup_ire_editable &&
                               setup_ire_check_->isChecked());
  video_path_hint_->setText(
      state.video_path_requires_y_suffix
          ? tr("Y/C output: the video path must end in “.y” (a matching "
               "“.c” chroma file is written alongside it).")
          : QString());
}

void ProjectSettingsEditor::CommitProjectInfo() {
  if (updating_) {
    return;
  }
  committing_ = true;
  document_->SetProjectInfo(name_edit_->text().toStdString(),
                            version_edit_->text().toStdString(),
                            description_edit_->toPlainText().toStdString());
  committing_ = false;
}

void ProjectSettingsEditor::CommitCvbsPresets() {
  if (updating_) {
    return;
  }

  CvbsPresets presets = document_->project().cvbs_presets;
  presets.video_standard_preset =
      StandardFromString(standard_combo_->currentText().toStdString());
  presets.sample_encoding_preset =
      sample_encoding_combo_->currentText().toStdString();
  presets.signal_state_preset =
      signal_state_combo_->currentText().toStdString();
  presets.pal_laserdisc_pilot_burst = pilot_burst_check_->isChecked();
  presets.ntsc_laserdisc_vbi_burst = vbi_burst_check_->isChecked();
  presets.ntsc_black_setup_ire_specified = setup_ire_check_->isChecked();
  presets.ntsc_black_setup_ire =
      setup_ire_check_->isChecked() ? setup_ire_combo_->currentData().toDouble()
                                    : 7.5;
  presets = NormalizeCvbsPresetsForStandard(presets);

  committing_ = true;
  document_->SetCvbsPresets(presets);
  committing_ = false;
  LoadFromDocument();
}

void ProjectSettingsEditor::CommitOutputTargets() {
  if (updating_) {
    return;
  }

  OutputTargets output = document_->project().output;
  output.signal_type = signal_type_combo_->currentText().toStdString();
  output.video_path = EnforceSignalTypeVideoPath(
      video_path_edit_->text().toStdString(), output.signal_type);
  // All outputs are co-located with the video; the metadata path is always
  // derived from it rather than set independently.
  output.metadata_path = DeriveMetadataPath(output.video_path);

  committing_ = true;
  document_->SetOutputTargets(output);
  committing_ = false;
  LoadFromDocument();
}

void ProjectSettingsEditor::OnStandardChanged() { CommitCvbsPresets(); }

void ProjectSettingsEditor::OnSignalTypeChanged() { CommitOutputTargets(); }

void ProjectSettingsEditor::OnBrowseVideoPath() {
  const bool yc = signal_type_combo_->currentText() == QStringLiteral("yc");
  const QString filter =
      yc ? tr("Y/C luma files (*.y);;All files (*)")
         : tr("Composite files (*.composite);;All files (*)");
  const QString path = QFileDialog::getSaveFileName(
      this, tr("Output Video"), video_path_edit_->text(), filter);
  if (path.isEmpty()) {
    return;
  }
  video_path_edit_->setText(path);
  CommitOutputTargets();
}

}  // namespace videosynth::gui
