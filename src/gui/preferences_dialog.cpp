/*
 * File:        preferences_dialog.cpp
 * Module:      gui
 * Purpose:     Settings dialog page for generation run preferences
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "preferences_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace videosynth::gui {

PreferencesDialog::PreferencesDialog(const GenerationPreferences& preferences,
                                     QWidget* parent)
    : QDialog(parent) {
  setWindowTitle(tr("Preferences"));

  auto* layout = new QVBoxLayout(this);

  auto* generation_group = new QGroupBox(tr("Generation"), this);
  auto* generation_form = new QFormLayout(generation_group);
  threads_spin_ = new QSpinBox(generation_group);
  threads_spin_->setRange(0, 256);
  threads_spin_->setSpecialValueText(tr("Auto"));
  threads_spin_->setToolTip(
      tr("Frame synthesis worker threads. Auto uses all hardware threads; "
         "output is byte-identical regardless of the thread count."));
  threads_spin_->setValue(preferences.threads);
  generation_form->addRow(tr("Synthesis threads:"), threads_spin_);
  layout->addWidget(generation_group);

  auto* logging_group = new QGroupBox(tr("Logging"), this);
  auto* logging_form = new QFormLayout(logging_group);
  log_level_combo_ = new QComboBox(logging_group);
  log_level_combo_->addItem(tr("Info"), QStringLiteral("info"));
  log_level_combo_->addItem(tr("Debug"), QStringLiteral("debug"));
  log_level_combo_->addItem(tr("Trace"), QStringLiteral("trace"));
  const int level_index =
      log_level_combo_->findData(SanitizedLogLevel(preferences.log_level));
  log_level_combo_->setCurrentIndex(level_index >= 0 ? level_index : 0);
  logging_form->addRow(tr("Log level:"), log_level_combo_);

  log_to_file_check_ = new QCheckBox(tr("Write log to file"), logging_group);
  log_to_file_check_->setChecked(preferences.log_to_file);
  logging_form->addRow(log_to_file_check_);

  auto* log_file_row = new QWidget(logging_group);
  auto* log_file_layout = new QHBoxLayout(log_file_row);
  log_file_layout->setContentsMargins(0, 0, 0, 0);
  log_file_edit_ = new QLineEdit(preferences.log_file_path, log_file_row);
  log_file_browse_ = new QPushButton(tr("Browse…"), log_file_row);
  log_file_layout->addWidget(log_file_edit_, 1);
  log_file_layout->addWidget(log_file_browse_);
  logging_form->addRow(tr("Log file:"), log_file_row);

  auto UpdateLogFileEnabled = [this] {
    const bool enabled = log_to_file_check_->isChecked();
    log_file_edit_->setEnabled(enabled);
    log_file_browse_->setEnabled(enabled);
  };
  UpdateLogFileEnabled();
  connect(log_to_file_check_, &QCheckBox::toggled, this, UpdateLogFileEnabled);
  connect(log_file_browse_, &QPushButton::clicked, this,
          &PreferencesDialog::OnBrowseLogFile);
  layout->addWidget(logging_group);

  auto* buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);
}

GenerationPreferences PreferencesDialog::preferences() const {
  GenerationPreferences preferences;
  preferences.threads = threads_spin_->value();
  preferences.log_level = log_level_combo_->currentData().toString();
  preferences.log_to_file = log_to_file_check_->isChecked();
  preferences.log_file_path = log_file_edit_->text();
  return preferences;
}

void PreferencesDialog::OnBrowseLogFile() {
  const QString path = QFileDialog::getSaveFileName(
      this, tr("Select Log File"), log_file_edit_->text(),
      tr("Log files (*.log *.txt);;All files (*)"));
  if (!path.isEmpty()) {
    log_file_edit_->setText(path);
  }
}

}  // namespace videosynth::gui
