/*
 * File:        programme_status_dialog.cpp
 * Module:      gui
 * Purpose:     Modal dialog for composing the programme status code from its
 *              IEC Amendment 2 fields
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "programme_status_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "videosynth/biphase_utils.h"

namespace videosynth::gui {

ProgrammeStatusDialog::ProgrammeStatusDialog(Standard standard,
                                             const QString& current_hex,
                                             QWidget* parent)
    : QDialog(parent) {
  setWindowTitle(tr("Programme Status Code"));
  setModal(true);

  // Seed from the current hex when it parses as a programme status word;
  // otherwise start from the spec defaults (CX on, 12-inch first side, no
  // teletext, copy prohibited, stereo).
  ProgrammeStatusFields fields;
  if (const auto code =
          ParseBiphaseHexCode(current_hex.trimmed().toStdString())) {
    if (const auto decoded = DecodeProgrammeStatusCode(*code)) {
      fields = *decoded;
    }
  }

  auto* layout = new QVBoxLayout(this);

  auto* hint = new QLabel(
      tr("Composes the 24-bit programme status word per IEC 60856/60857 "
         "Amendment 2 Appendix C.1. The X₅ Hamming check nibble is "
         "computed automatically."),
      this);
  hint->setWordWrap(true);
  hint->setEnabled(false);  // muted help text
  layout->addWidget(hint);

  auto* form = new QFormLayout();

  cx_check_ = new QCheckBox(tr("CX noise reduction on"), this);
  cx_check_->setChecked(fields.cx_on);
  cx_check_->setToolTip(tr("On encodes the DC nibble pair, off encodes BA."));
  form->addRow(tr("CX:"), cx_check_);

  disc_size_combo_ = new QComboBox(this);
  disc_size_combo_->addItem(tr("12 inch"));  // X31 = 0
  disc_size_combo_->addItem(tr("8 inch"));   // X31 = 1
  disc_size_combo_->setCurrentIndex(fields.disc_size_8_inch ? 1 : 0);
  form->addRow(tr("Disc size (X₃₁):"), disc_size_combo_);

  disc_side_combo_ = new QComboBox(this);
  disc_side_combo_->addItem(tr("First side"));   // X32 = 0
  disc_side_combo_->addItem(tr("Second side"));  // X32 = 1
  disc_side_combo_->setCurrentIndex(fields.second_side ? 1 : 0);
  form->addRow(tr("Disc side (X₃₂):"), disc_side_combo_);

  teletext_check_ = new QCheckBox(tr("Teletext signals present"), this);
  teletext_check_->setChecked(fields.teletext_present);
  teletext_check_->setToolTip(
      tr("Set when teletext signals are present anywhere on the disc."));
  form->addRow(tr("Teletext (X₃₃):"), teletext_check_);

  copy_check_ = new QCheckBox(tr("Copying permitted"), this);
  copy_check_->setChecked(fields.copy_permitted);
  copy_check_->setToolTip(tr("Clear = copy prohibited, set = copy permitted."));
  form->addRow(tr("Copy (X₃₄):"), copy_check_);

  audio_video_combo_ = new QComboBox(this);
  const std::vector<std::string> labels = AudioVideoModeLabels(standard);
  for (std::size_t mode = 0; mode < labels.size(); ++mode) {
    audio_video_combo_->addItem(
        tr("%1 — %2").arg(mode).arg(QString::fromStdString(labels[mode])));
  }
  audio_video_combo_->setCurrentIndex(fields.audio_video_mode);
  form->addRow(tr("Audio/video mode (X₄):"), audio_video_combo_);

  preview_label_ = new QLabel(this);
  form->addRow(tr("Status word:"), preview_label_);

  layout->addLayout(form);

  auto* buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);

  const auto refresh = [this] { UpdatePreview(); };
  connect(cx_check_, &QCheckBox::toggled, this, refresh);
  connect(disc_size_combo_, &QComboBox::currentIndexChanged, this, refresh);
  connect(disc_side_combo_, &QComboBox::currentIndexChanged, this, refresh);
  connect(teletext_check_, &QCheckBox::toggled, this, refresh);
  connect(copy_check_, &QCheckBox::toggled, this, refresh);
  connect(audio_video_combo_, &QComboBox::currentIndexChanged, this, refresh);

  UpdatePreview();
}

QString ProgrammeStatusDialog::hex_text() const {
  return QString::fromStdString(
      FormatProgrammeStatusHex(BuildProgrammeStatusCode(FieldsFromControls())));
}

ProgrammeStatusFields ProgrammeStatusDialog::FieldsFromControls() const {
  ProgrammeStatusFields fields;
  fields.cx_on = cx_check_->isChecked();
  fields.disc_size_8_inch = disc_size_combo_->currentIndex() == 1;
  fields.second_side = disc_side_combo_->currentIndex() == 1;
  fields.teletext_present = teletext_check_->isChecked();
  fields.copy_permitted = copy_check_->isChecked();
  fields.audio_video_mode =
      static_cast<uint8_t>(audio_video_combo_->currentIndex() & 0x0F);
  return fields;
}

void ProgrammeStatusDialog::UpdatePreview() {
  preview_label_->setText(hex_text());
}

}  // namespace videosynth::gui
