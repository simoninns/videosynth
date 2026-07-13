/*
 * File:        line_injections_editor.cpp
 * Module:      gui
 * Purpose:     Editor for a section's line injections (VITS and laserdisc
 *              biphase codes)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "line_injections_editor.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

#include "line_injection_presenter.h"

namespace videosynth::gui {

namespace {

constexpr int kCodeTypeColumn = 0;
constexpr int kCodeValueColumn = 1;

// Returns the single optional parameter carried by the code, as display
// text, plus a placeholder describing it.
QString CodeValueText(const Section::LineInjectionCode& code) {
  if (CodeTypeUsesStartValue(code.code_type) && code.start_value_specified) {
    return QString::number(code.start_value);
  }
  if (CodeTypeUsesChapter(code.code_type) && code.chapter_specified) {
    return QString::number(code.chapter);
  }
  if (CodeTypeUsesProgrammeStatus(code.code_type) &&
      code.programme_status_specified) {
    return QString::fromStdString(code.programme_status);
  }
  if (CodeTypeUsesUsersCode(code.code_type) && code.users_code_specified) {
    return QString::fromStdString(code.users_code);
  }
  return {};
}

QString CodeValuePlaceholder(const std::string& code_type) {
  if (CodeTypeUsesStartValue(code_type)) {
    return QStringLiteral("start_value (e.g. 1)");
  }
  if (CodeTypeUsesChapter(code_type)) {
    return QStringLiteral("chapter 0–79");
  }
  if (CodeTypeUsesProgrammeStatus(code_type)) {
    return QStringLiteral("hex status (e.g. 0x8DC000)");
  }
  if (CodeTypeUsesUsersCode(code_type)) {
    return QStringLiteral("hex code (e.g. 0x80D000)");
  }
  return {};
}

// Writes `text` into the code's parameter field for its code type; an empty
// text clears the *_specified flag so the emitter drops the key.
void ApplyCodeValue(Section::LineInjectionCode* code, const QString& text) {
  const QString trimmed = text.trimmed();
  code->start_value = 0;
  code->start_value_specified = false;
  code->chapter = 0;
  code->chapter_specified = false;
  code->programme_status.clear();
  code->programme_status_specified = false;
  code->users_code.clear();
  code->users_code_specified = false;

  if (trimmed.isEmpty()) {
    return;
  }
  if (CodeTypeUsesStartValue(code->code_type)) {
    bool ok = false;
    const int value = trimmed.toInt(&ok);
    if (ok) {
      code->start_value = value;
      code->start_value_specified = true;
    }
  } else if (CodeTypeUsesChapter(code->code_type)) {
    bool ok = false;
    const int value = trimmed.toInt(&ok);
    if (ok) {
      code->chapter = value;
      code->chapter_specified = true;
    }
  } else if (CodeTypeUsesProgrammeStatus(code->code_type)) {
    code->programme_status = trimmed.toStdString();
    code->programme_status_specified = true;
  } else if (CodeTypeUsesUsersCode(code->code_type)) {
    code->users_code = trimmed.toStdString();
    code->users_code_specified = true;
  }
}

}  // namespace

LineInjectionsEditor::LineInjectionsEditor(QWidget* parent) : QWidget(parent) {
  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  // Left: injection list with add/remove.
  auto* list_panel = new QVBoxLayout();
  injection_list_ = new QListWidget(this);
  injection_list_->setSelectionMode(QAbstractItemView::SingleSelection);
  list_panel->addWidget(injection_list_);

  auto* list_buttons = new QHBoxLayout();
  auto* add_injection_button = new QPushButton(tr("Add"), this);
  remove_injection_button_ = new QPushButton(tr("Remove"), this);
  list_buttons->addWidget(add_injection_button);
  list_buttons->addWidget(remove_injection_button_);
  list_buttons->addStretch();
  list_panel->addLayout(list_buttons);
  layout->addLayout(list_panel, 1);

  // Right: per-injection form.
  form_panel_ = new QWidget(this);
  auto* form_layout = new QVBoxLayout(form_panel_);
  form_layout->setContentsMargins(0, 0, 0, 0);

  auto* type_form = new QFormLayout();
  type_combo_ = new QComboBox(form_panel_);
  for (const std::string& type : AvailableInjectionTypes()) {
    type_combo_->addItem(QString::fromStdString(type));
  }
  type_form->addRow(tr("Injection type:"), type_combo_);
  form_layout->addLayout(type_form);

  vits_panel_ = new QWidget(form_panel_);
  auto* vits_form = new QFormLayout(vits_panel_);
  vits_form->setContentsMargins(0, 0, 0, 0);
  vits_type_combo_ = new QComboBox(vits_panel_);
  target_lines_edit_ = new QLineEdit(vits_panel_);
  target_lines_edit_->setPlaceholderText(tr("e.g. 19, 282"));
  vits_line_hint_ = new QLabel(vits_panel_);
  vits_line_hint_->setWordWrap(true);
  vits_form->addRow(tr("VITS type:"), vits_type_combo_);
  vits_form->addRow(tr("Target lines:"), target_lines_edit_);
  vits_form->addRow(QString(), vits_line_hint_);
  form_layout->addWidget(vits_panel_);

  laserdisc_panel_ = new QWidget(form_panel_);
  auto* laserdisc_layout = new QVBoxLayout(laserdisc_panel_);
  laserdisc_layout->setContentsMargins(0, 0, 0, 0);
  auto* disc_form = new QFormLayout();
  disc_type_combo_ = new QComboBox(laserdisc_panel_);
  for (const std::string& disc_type : AvailableDiscTypes()) {
    disc_type_combo_->addItem(QString::fromStdString(disc_type));
  }
  disc_form->addRow(tr("Disc type:"), disc_type_combo_);
  laserdisc_layout->addLayout(disc_form);

  codes_table_ = new QTableWidget(0, 2, laserdisc_panel_);
  codes_table_->setHorizontalHeaderLabels({tr("Code type"), tr("Value")});
  codes_table_->horizontalHeader()->setStretchLastSection(true);
  codes_table_->verticalHeader()->setVisible(false);
  codes_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  codes_table_->setSelectionMode(QAbstractItemView::SingleSelection);
  laserdisc_layout->addWidget(codes_table_);

  auto* code_buttons = new QHBoxLayout();
  auto* add_code_button = new QPushButton(tr("Add code"), laserdisc_panel_);
  remove_code_button_ = new QPushButton(tr("Remove code"), laserdisc_panel_);
  code_buttons->addWidget(add_code_button);
  code_buttons->addWidget(remove_code_button_);
  code_buttons->addStretch();
  laserdisc_layout->addLayout(code_buttons);

  form_layout->addWidget(laserdisc_panel_);
  form_layout->addStretch();
  layout->addWidget(form_panel_, 2);

  connect(add_injection_button, &QPushButton::clicked, this,
          &LineInjectionsEditor::OnAddInjection);
  connect(remove_injection_button_, &QPushButton::clicked, this,
          &LineInjectionsEditor::OnRemoveInjection);
  connect(injection_list_, &QListWidget::currentRowChanged, this,
          [this](int) { LoadInjectionForm(); });
  connect(type_combo_, &QComboBox::activated, this,
          &LineInjectionsEditor::OnTypeChanged);
  connect(vits_type_combo_, &QComboBox::activated, this,
          &LineInjectionsEditor::OnVitsTypeChanged);
  connect(target_lines_edit_, &QLineEdit::editingFinished, this,
          &LineInjectionsEditor::OnTargetLinesEdited);
  connect(disc_type_combo_, &QComboBox::activated, this,
          &LineInjectionsEditor::OnDiscTypeChanged);
  connect(add_code_button, &QPushButton::clicked, this,
          &LineInjectionsEditor::OnAddCode);
  connect(remove_code_button_, &QPushButton::clicked, this,
          &LineInjectionsEditor::OnRemoveCode);

  LoadInjectionForm();
}

void LineInjectionsEditor::SetContext(Standard standard,
                                      SectionType section_type) {
  standard_ = standard;
  section_type_ = section_type;
  LoadInjectionForm();
}

void LineInjectionsEditor::SetInjections(
    std::vector<Section::LineInjection> injections) {
  // Preserve the selected row across a refresh: editing an injection
  // round-trips through the owning section editor, which calls this to reload,
  // and resetting to row 0 would yank the user off the injection they are
  // editing. Injections never reorder on edit, so the row index stays valid
  // (clamped for a shorter list).
  const int previous_row = injection_list_->currentRow();
  injections_ = std::move(injections);
  const int select_row =
      injections_.empty()
          ? -1
          : qBound(0, previous_row, static_cast<int>(injections_.size()) - 1);
  RebuildInjectionList(select_row);
}

void LineInjectionsEditor::RebuildInjectionList(int select_row) {
  updating_ = true;
  injection_list_->clear();
  for (const Section::LineInjection& injection : injections_) {
    injection_list_->addItem(InjectionSummary(injection));
  }
  updating_ = false;
  injection_list_->setCurrentRow(
      qBound(-1, select_row, injection_list_->count() - 1));
  LoadInjectionForm();
}

Section::LineInjection* LineInjectionsEditor::CurrentInjection() {
  const int row = injection_list_->currentRow();
  if (row < 0 || row >= static_cast<int>(injections_.size())) {
    return nullptr;
  }
  return &injections_[static_cast<std::size_t>(row)];
}

void LineInjectionsEditor::LoadInjectionForm() {
  updating_ = true;
  Section::LineInjection* injection = CurrentInjection();
  form_panel_->setEnabled(injection != nullptr);
  remove_injection_button_->setEnabled(injection != nullptr);

  if (injection == nullptr) {
    vits_panel_->setVisible(false);
    laserdisc_panel_->setVisible(false);
    updating_ = false;
    return;
  }

  const bool is_laserdisc = injection->type == "laserdisc";
  type_combo_->setCurrentText(QString::fromStdString(injection->type));
  vits_panel_->setVisible(!is_laserdisc);
  laserdisc_panel_->setVisible(is_laserdisc);

  if (is_laserdisc) {
    disc_type_combo_->setCurrentText(
        QString::fromStdString(injection->disc_type));
    RebuildCodesTable(*injection);
  } else {
    // Standard-filtered VITS catalogue; unknown values are preserved so a
    // loaded file never loses information.
    vits_type_combo_->clear();
    for (const std::string& vits_type : AvailableVitsTypes(standard_)) {
      vits_type_combo_->addItem(QString::fromStdString(vits_type));
    }
    const QString current = QString::fromStdString(injection->vits_type);
    int index = vits_type_combo_->findText(current);
    if (index < 0 && !current.isEmpty()) {
      vits_type_combo_->addItem(current);
      index = vits_type_combo_->count() - 1;
    }
    vits_type_combo_->setCurrentIndex(index);

    target_lines_edit_->setText(
        QString::fromStdString(FormatTargetLines(injection->target_lines)));
    const int recommended =
        RecommendedVitsLine(standard_, injection->vits_type);
    vits_line_hint_->setText(
        recommended > 0
            ? tr("This VITS type must target frame line %1.").arg(recommended)
            : QString());
  }
  updating_ = false;
}

void LineInjectionsEditor::RebuildCodesTable(
    const Section::LineInjection& injection) {
  codes_table_->setRowCount(static_cast<int>(injection.codes.size()));

  const DiscType disc_type = DiscTypeFromString(injection.disc_type);
  const std::vector<std::string> code_types =
      AvailableLaserdiscCodeTypes(disc_type, section_type_, standard_);

  for (int row = 0; row < static_cast<int>(injection.codes.size()); ++row) {
    const Section::LineInjectionCode& code =
        injection.codes[static_cast<std::size_t>(row)];

    auto* type_combo = new QComboBox(codes_table_);
    for (const std::string& code_type : code_types) {
      type_combo->addItem(QString::fromStdString(code_type));
    }
    const QString current = QString::fromStdString(code.code_type);
    int index = type_combo->findText(current);
    if (index < 0 && !current.isEmpty()) {
      type_combo->addItem(current);
      index = type_combo->count() - 1;
    }
    type_combo->setCurrentIndex(index);
    connect(type_combo, &QComboBox::activated, this,
            [this, row] { OnCodeTypeChanged(row); });
    codes_table_->setCellWidget(row, kCodeTypeColumn, type_combo);

    auto* value_edit = new QLineEdit(codes_table_);
    value_edit->setText(CodeValueText(code));
    value_edit->setPlaceholderText(CodeValuePlaceholder(code.code_type));
    value_edit->setEnabled(!CodeValuePlaceholder(code.code_type).isEmpty());
    connect(value_edit, &QLineEdit::editingFinished, this,
            [this, row] { OnCodeValueEdited(row); });
    codes_table_->setCellWidget(row, kCodeValueColumn, value_edit);
  }
  remove_code_button_->setEnabled(!injection.codes.empty());
}

void LineInjectionsEditor::AnnounceEdit() {
  if (!updating_) {
    emit InjectionsEdited();
  }
}

void LineInjectionsEditor::AddDefaultInjection() { OnAddInjection(); }

void LineInjectionsEditor::OnAddInjection() {
  Section::LineInjection injection;
  injection.type = "vits";
  const std::vector<std::string> vits_types = AvailableVitsTypes(standard_);
  if (!vits_types.empty()) {
    injection.vits_type = vits_types.front();
    const int recommended = RecommendedVitsLine(standard_, injection.vits_type);
    if (recommended > 0) {
      injection.target_lines = {recommended};
    }
  }
  injections_.push_back(std::move(injection));
  RebuildInjectionList(static_cast<int>(injections_.size()) - 1);
  AnnounceEdit();
}

void LineInjectionsEditor::OnRemoveInjection() {
  const int row = injection_list_->currentRow();
  if (row < 0 || row >= static_cast<int>(injections_.size())) {
    return;
  }
  injections_.erase(injections_.begin() + row);
  RebuildInjectionList(qMin(row, static_cast<int>(injections_.size()) - 1));
  AnnounceEdit();
}

void LineInjectionsEditor::OnTypeChanged() {
  Section::LineInjection* injection = CurrentInjection();
  if (injection == nullptr || updating_) {
    return;
  }

  const std::string type = type_combo_->currentText().toStdString();
  if (injection->type == type) {
    return;
  }

  // Reset type-specific fields: laserdisc injections carry no target lines
  // (placement fixed by IEC 60856/60857 §10) and vits carries no codes.
  *injection = Section::LineInjection{};
  injection->type = type;
  if (type == "laserdisc") {
    injection->disc_type = "CAV";
  } else if (type == "vits") {
    const std::vector<std::string> vits_types = AvailableVitsTypes(standard_);
    if (!vits_types.empty()) {
      injection->vits_type = vits_types.front();
      const int recommended =
          RecommendedVitsLine(standard_, injection->vits_type);
      if (recommended > 0) {
        injection->target_lines = {recommended};
      }
    }
  }

  const int row = injection_list_->currentRow();
  RebuildInjectionList(row);
  AnnounceEdit();
}

void LineInjectionsEditor::OnVitsTypeChanged() {
  Section::LineInjection* injection = CurrentInjection();
  if (injection == nullptr || updating_) {
    return;
  }
  injection->vits_type = vits_type_combo_->currentText().toStdString();

  // The validator's strict policy: types with a defined placement line must
  // target exactly that line, so pre-fill it.
  const int recommended = RecommendedVitsLine(standard_, injection->vits_type);
  if (recommended > 0) {
    injection->target_lines = {recommended};
  }

  const int row = injection_list_->currentRow();
  RebuildInjectionList(row);
  AnnounceEdit();
}

void LineInjectionsEditor::OnTargetLinesEdited() {
  Section::LineInjection* injection = CurrentInjection();
  if (injection == nullptr || updating_) {
    return;
  }
  std::vector<int> lines;
  if (!ParseTargetLines(target_lines_edit_->text().toStdString(), &lines)) {
    // Restore the last-known-good value; validation feedback for range
    // problems comes from the issues panel.
    target_lines_edit_->setText(
        QString::fromStdString(FormatTargetLines(injection->target_lines)));
    return;
  }
  if (injection->target_lines == lines) {
    return;
  }
  injection->target_lines = std::move(lines);
  injection_list_->currentItem()->setText(InjectionSummary(*injection));
  AnnounceEdit();
}

void LineInjectionsEditor::OnDiscTypeChanged() {
  Section::LineInjection* injection = CurrentInjection();
  if (injection == nullptr || updating_) {
    return;
  }
  const std::string disc_type = disc_type_combo_->currentText().toStdString();
  if (injection->disc_type == disc_type) {
    return;
  }
  injection->disc_type = disc_type;

  // Drop codes the new disc type cannot carry (e.g. picture_number on CLV).
  const DiscType parsed = DiscTypeFromString(disc_type);
  auto& codes = injection->codes;
  codes.erase(std::remove_if(codes.begin(), codes.end(),
                             [parsed](const Section::LineInjectionCode& code) {
                               if (parsed == DiscType::kCAV) {
                                 return !IsValidCavCodeType(code.code_type);
                               }
                               if (parsed == DiscType::kCLV) {
                                 return !IsValidClvCodeType(code.code_type);
                               }
                               return false;
                             }),
              codes.end());

  const int row = injection_list_->currentRow();
  RebuildInjectionList(row);
  AnnounceEdit();
}

void LineInjectionsEditor::OnAddCode() {
  Section::LineInjection* injection = CurrentInjection();
  if (injection == nullptr) {
    return;
  }
  const std::vector<std::string> code_types = AvailableLaserdiscCodeTypes(
      DiscTypeFromString(injection->disc_type), section_type_, standard_);

  Section::LineInjectionCode code;
  code.code_type = code_types.empty() ? "picture_number" : code_types.front();
  injection->codes.push_back(code);
  RebuildCodesTable(*injection);
  AnnounceEdit();
}

void LineInjectionsEditor::OnRemoveCode() {
  Section::LineInjection* injection = CurrentInjection();
  if (injection == nullptr) {
    return;
  }
  const int row = codes_table_->currentRow();
  if (row < 0 || row >= static_cast<int>(injection->codes.size())) {
    return;
  }
  injection->codes.erase(injection->codes.begin() + row);
  RebuildCodesTable(*injection);
  AnnounceEdit();
}

void LineInjectionsEditor::OnCodeTypeChanged(int row) {
  Section::LineInjection* injection = CurrentInjection();
  if (injection == nullptr || updating_ || row < 0 ||
      row >= static_cast<int>(injection->codes.size())) {
    return;
  }
  auto* type_combo =
      qobject_cast<QComboBox*>(codes_table_->cellWidget(row, kCodeTypeColumn));
  if (type_combo == nullptr) {
    return;
  }

  Section::LineInjectionCode& code =
      injection->codes[static_cast<std::size_t>(row)];
  code.code_type = type_combo->currentText().toStdString();
  // Changing the type invalidates the old parameter.
  ApplyCodeValue(&code, QString());
  RebuildCodesTable(*injection);
  AnnounceEdit();
}

void LineInjectionsEditor::OnCodeValueEdited(int row) {
  Section::LineInjection* injection = CurrentInjection();
  if (injection == nullptr || updating_ || row < 0 ||
      row >= static_cast<int>(injection->codes.size())) {
    return;
  }
  auto* value_edit =
      qobject_cast<QLineEdit*>(codes_table_->cellWidget(row, kCodeValueColumn));
  if (value_edit == nullptr) {
    return;
  }

  Section::LineInjectionCode& code =
      injection->codes[static_cast<std::size_t>(row)];
  const Section::LineInjectionCode before = code;
  ApplyCodeValue(&code, value_edit->text());
  if (!(code == before)) {
    AnnounceEdit();
  }
}

QString LineInjectionsEditor::InjectionSummary(
    const Section::LineInjection& injection) const {
  if (injection.type == "laserdisc") {
    return QStringLiteral("laserdisc: %1 (%2 codes)")
        .arg(QString::fromStdString(injection.disc_type.empty()
                                        ? std::string("?")
                                        : injection.disc_type))
        .arg(injection.codes.size());
  }
  QString summary = QString::fromStdString(injection.type);
  if (!injection.vits_type.empty()) {
    summary +=
        QStringLiteral(": %1").arg(QString::fromStdString(injection.vits_type));
  }
  if (!injection.target_lines.empty()) {
    summary += QStringLiteral(" → %1").arg(
        QString::fromStdString(FormatTargetLines(injection.target_lines)));
  }
  return summary;
}

}  // namespace videosynth::gui
