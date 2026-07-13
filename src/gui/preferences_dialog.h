/*
 * File:        preferences_dialog.h
 * Module:      gui
 * Purpose:     Settings dialog page for generation run preferences
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <QDialog>

#include "generation_preferences.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace videosynth::gui {

// Modal preferences dialog: frame synthesis thread count (auto/N), default
// log level, and an optional log file. The caller persists the accepted
// values (see generation_preferences.h).
//
// Thread-safety: NOT thread-safe. GUI (main) thread only.
class PreferencesDialog : public QDialog {
  Q_OBJECT

 public:
  explicit PreferencesDialog(const GenerationPreferences& preferences,
                             QWidget* parent = nullptr);

  // Preferences as currently edited in the dialog's widgets.
  GenerationPreferences preferences() const;

 private slots:
  void OnBrowseLogFile();

 private:
  QSpinBox* threads_spin_ = nullptr;
  QComboBox* log_level_combo_ = nullptr;
  QCheckBox* log_to_file_check_ = nullptr;
  QLineEdit* log_file_edit_ = nullptr;
  QPushButton* log_file_browse_ = nullptr;
};

}  // namespace videosynth::gui
