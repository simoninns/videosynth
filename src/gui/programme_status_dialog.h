/*
 * File:        programme_status_dialog.h
 * Module:      gui
 * Purpose:     Modal dialog for composing the programme status code from its
 *              IEC Amendment 2 fields
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <QDialog>

#include "programme_status_presenter.h"
#include "videosynth/model.h"

class QCheckBox;
class QComboBox;
class QLabel;

namespace videosynth::gui {

// Composes a 24-bit programme status word from its Amendment 2 fields
// (IEC 60856/60857 Amendment 2 Appendix C.1): CX noise reduction, disc size,
// disc side, teletext presence, copy permission, and the X4 audio/video mode.
// The X5 Hamming check nibble is derived from X4 and shown, not edited. A
// read-only preview tracks the resulting hex word as fields change.
//
// Seed the dialog with the current hex text (parsed leniently; unparseable
// text leaves spec defaults) and read hex_text() back after accept.
//
// Thread-safety: NOT thread-safe. GUI (main) thread only.
class ProgrammeStatusDialog : public QDialog {
  Q_OBJECT

 public:
  // `standard` selects the standard-specific audio/video mode labels;
  // `current_hex` seeds the fields when it parses as a programme status word.
  ProgrammeStatusDialog(Standard standard, const QString& current_hex,
                        QWidget* parent = nullptr);

  // The composed hex word ("0x8DC000" form); only meaningful after accept.
  QString hex_text() const;

 private:
  // Reflects the current field controls into the hex preview label.
  void UpdatePreview();

  // Current controls state as presenter fields.
  ProgrammeStatusFields FieldsFromControls() const;

  QCheckBox* cx_check_ = nullptr;
  QComboBox* disc_size_combo_ = nullptr;
  QComboBox* disc_side_combo_ = nullptr;
  QCheckBox* teletext_check_ = nullptr;
  QCheckBox* copy_check_ = nullptr;
  QComboBox* audio_video_combo_ = nullptr;
  QLabel* preview_label_ = nullptr;
};

}  // namespace videosynth::gui
