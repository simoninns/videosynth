/*
 * File:        section_editor.cpp
 * Module:      gui
 * Purpose:     Per-section form editor: source and span, optional audio,
 *              noise, dropout, OSD blocks, and line injections
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "section_editor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <filesystem>
#include <string>

#include "asset_roots.h"
#include "section_block_presenters.h"
#include "section_list_model.h"
#include "videosynth/path_resolution.h"

namespace videosynth::gui {

namespace {

constexpr int kMaxDurationFrames = 10000000;
constexpr int kMaxOverlayOffset = 9999;

// OSD overlay table columns.
enum OsdColumn {
  kOsdTextColumn = 0,
  kOsdXColumn,
  kOsdYColumn,
  kOsdScaleColumn,
  kOsdFgColumn,
  kOsdBgColumn,
  kOsdColumnCount,
};

int64_t ParseSeed(const QString& text) {
  bool ok = false;
  const qlonglong value = text.trimmed().toLongLong(&ok);
  return ok ? static_cast<int64_t>(value) : 0;
}

}  // namespace

SectionEditor::SectionEditor(ProjectDocument* document,
                             SourceProbeController* probe_controller,
                             QWidget* parent)
    : QWidget(parent),
      document_(document),
      probe_controller_(probe_controller) {
  BuildUi();
  SetCurrentSection(-1);

  connect(document_, &ProjectDocument::SectionEdited, this, [this](int index) {
    if (index == section_index_ && !committing_) {
      LoadFromDocument();
    }
  });
  // Standard changes alter the injection catalogues and probe verdict.
  connect(document_, &ProjectDocument::ProjectSettingsChanged, this, [this] {
    if (section_index_ >= 0 && !committing_) {
      LoadFromDocument();
    }
  });
  connect(probe_controller_, &SourceProbeController::ProbeStarted, this,
          [this] {
            probe_status_label_->setText(tr("Probing source…"));
            probe_detail_label_->clear();
          });
  connect(probe_controller_, &SourceProbeController::ReportChanged, this,
          [this] { UpdateProbeDisplay(); });
}

void SectionEditor::BuildUi() {
  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);

  placeholder_ = new QLabel(tr("Select a section in the Sections list."), this);
  placeholder_->setAlignment(Qt::AlignCenter);
  outer->addWidget(placeholder_);

  auto* scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  content_ = new QWidget(scroll);
  auto* layout = new QVBoxLayout(content_);

  layout->addWidget(BuildGeneralGroup());
  layout->addWidget(BuildProbeGroup());
  layout->addWidget(BuildAudioGroup());
  layout->addWidget(BuildNoiseGroup());
  layout->addWidget(BuildDropoutsGroup());
  layout->addWidget(BuildOsdGroup());
  layout->addWidget(BuildInjectionsGroup());
  layout->addStretch();

  scroll->setWidget(content_);
  outer->addWidget(scroll);
}

QWidget* SectionEditor::BuildGeneralGroup() {
  auto* group = new QGroupBox(tr("Section"), content_);
  auto* form = new QFormLayout(group);

  name_edit_ = new QLineEdit(group);
  form->addRow(tr("Name:"), name_edit_);

  section_type_combo_ = new QComboBox(group);
  section_type_combo_->addItem(tr("(none)"));
  section_type_combo_->addItem(QStringLiteral("lead_in"));
  section_type_combo_->addItem(QStringLiteral("programme_area"));
  section_type_combo_->addItem(QStringLiteral("lead_out"));
  form->addRow(tr("Disc section type:"), section_type_combo_);

  source_root_combo_ = new QComboBox(group);
  source_root_combo_->addItem(tr("Project"), QStringLiteral("project"));
  source_root_combo_->addItem(tr("Bundled"), QStringLiteral("bundled"));
  source_root_combo_->addItem(tr("User"), QStringLiteral("user"));
  source_root_combo_->addItem(tr("Absolute"), QString());
  source_root_combo_->setToolTip(
      tr("Logical asset root the source path is relative to. Project = next to "
         "the project file; Bundled = shipped assets; User = your asset "
         "library; Absolute = a full path."));
  source_edit_ = new QLineEdit(group);
  auto* browse = new QPushButton(tr("Browse…"), group);
  auto* source_row = new QHBoxLayout();
  source_row->addWidget(source_root_combo_);
  source_row->addWidget(source_edit_);
  source_row->addWidget(browse);
  form->addRow(tr("Source:"), source_row);

  source_resolved_hint_ = new QLabel(group);
  source_resolved_hint_->setWordWrap(true);
  source_resolved_hint_->setEnabled(false);
  form->addRow(QString(), source_resolved_hint_);

  duration_spin_ = new QSpinBox(group);
  duration_spin_->setRange(1, kMaxDurationFrames);
  duration_all_check_ = new QCheckBox(tr("All source frames"), group);
  auto* duration_row = new QHBoxLayout();
  duration_row->addWidget(duration_spin_);
  duration_row->addWidget(duration_all_check_);
  duration_row->addStretch();
  form->addRow(tr("Duration (frames):"), duration_row);

  start_frame_label_ = new QLabel(group);
  form->addRow(tr("Start frame:"), start_frame_label_);

  connect(name_edit_, &QLineEdit::editingFinished, this,
          &SectionEditor::CommitSection);
  connect(section_type_combo_, &QComboBox::activated, this,
          &SectionEditor::CommitSection);
  connect(source_edit_, &QLineEdit::editingFinished, this, [this] {
    CommitSection();
    RequestProbe();
    UpdateSourceResolvedHint();
  });
  connect(source_root_combo_, &QComboBox::activated, this, [this] {
    if (!updating_) {
      CommitSection();
      RequestProbe();
      UpdateSourceResolvedHint();
    }
  });
  connect(browse, &QPushButton::clicked, this, &SectionEditor::OnBrowseSource);
  connect(duration_spin_, &QSpinBox::valueChanged, this, [this](int) {
    if (!updating_) {
      CommitSection();
    }
  });
  connect(duration_all_check_, &QCheckBox::toggled, this, [this](bool all) {
    duration_spin_->setEnabled(!all);
    if (!updating_) {
      CommitSection();
    }
  });
  return group;
}

QWidget* SectionEditor::BuildProbeGroup() {
  auto* group = new QGroupBox(tr("Source Profile"), content_);
  auto* layout = new QVBoxLayout(group);
  probe_status_label_ = new QLabel(group);
  probe_detail_label_ = new QLabel(group);
  probe_detail_label_->setWordWrap(true);
  layout->addWidget(probe_status_label_);
  layout->addWidget(probe_detail_label_);
  return group;
}

QWidget* SectionEditor::BuildAudioGroup() {
  audio_group_ = new QGroupBox(tr("Audio channel pairs"), content_);
  auto* layout = new QVBoxLayout(audio_group_);
  audio_editor_ = new AudioChannelPairsEditor(audio_group_);
  audio_editor_->setMinimumHeight(260);
  layout->addWidget(audio_editor_);

  connect(audio_editor_, &AudioChannelPairsEditor::ChannelPairsEdited, this,
          [this] {
            if (!updating_) {
              CommitSection();
            }
          });
  return audio_group_;
}

QWidget* SectionEditor::BuildNoiseGroup() {
  noise_group_ = new QGroupBox(tr("Noise injection"), content_);
  noise_group_->setCheckable(true);
  auto* form = new QFormLayout(noise_group_);

  noise_db_spin_ = new QDoubleSpinBox(noise_group_);
  noise_db_spin_->setRange(editor_limits::kNoiseDbMin,
                           editor_limits::kNoiseDbMax);
  noise_db_spin_->setDecimals(1);
  noise_db_spin_->setSuffix(tr(" dB"));
  form->addRow(tr("Noise floor (Black PSNR):"), noise_db_spin_);

  noise_spread_spin_ = new QDoubleSpinBox(noise_group_);
  // noise_db − spread must stay ≥ the 20 dB floor (validator rule).
  noise_spread_spin_->setRange(
      0.0, editor_limits::kNoiseDbMax - editor_limits::kNoiseDbMin);
  noise_spread_spin_->setDecimals(1);
  noise_spread_spin_->setSuffix(tr(" dB"));
  form->addRow(tr("White spread:"), noise_spread_spin_);

  noise_seed_check_ = new QCheckBox(tr("Fixed seed"), noise_group_);
  noise_seed_edit_ = new QLineEdit(noise_group_);
  auto* seed_row = new QHBoxLayout();
  seed_row->addWidget(noise_seed_check_);
  seed_row->addWidget(noise_seed_edit_);
  form->addRow(seed_row);

  const auto commit = [this] {
    if (!updating_) {
      CommitSection();
    }
  };
  connect(noise_group_, &QGroupBox::toggled, this, commit);
  connect(noise_db_spin_, &QDoubleSpinBox::valueChanged, this, commit);
  connect(noise_spread_spin_, &QDoubleSpinBox::valueChanged, this, commit);
  connect(noise_seed_check_, &QCheckBox::toggled, this, [this](bool checked) {
    noise_seed_edit_->setEnabled(checked);
    if (!updating_) {
      CommitSection();
    }
  });
  connect(noise_seed_edit_, &QLineEdit::editingFinished, this, commit);
  return noise_group_;
}

QWidget* SectionEditor::BuildDropoutsGroup() {
  auto* group = new QGroupBox(tr("Dropouts"), content_);
  auto* layout = new QVBoxLayout(group);

  const auto commit = [this] {
    if (!updating_) {
      CommitSection();
    }
  };

  const auto build_block = [&](const QString& title, QGroupBox** out_group,
                               QSpinBox** out_scale, QCheckBox** out_check,
                               QLineEdit** out_edit) {
    auto* block = new QGroupBox(title, group);
    block->setCheckable(true);
    auto* form = new QFormLayout(block);
    auto* scale = new QSpinBox(block);
    scale->setRange(editor_limits::kDropoutScaleMin,
                    editor_limits::kDropoutScaleMax);
    form->addRow(tr("Scale:"), scale);
    auto* check = new QCheckBox(tr("Fixed seed"), block);
    auto* edit = new QLineEdit(block);
    auto* seed_row = new QHBoxLayout();
    seed_row->addWidget(check);
    seed_row->addWidget(edit);
    form->addRow(seed_row);

    connect(block, &QGroupBox::toggled, this, commit);
    connect(scale, &QSpinBox::valueChanged, this, commit);
    connect(check, &QCheckBox::toggled, this, [this, edit](bool checked) {
      edit->setEnabled(checked);
      if (!updating_) {
        CommitSection();
      }
    });
    connect(edit, &QLineEdit::editingFinished, this, commit);

    *out_group = block;
    *out_scale = scale;
    *out_check = check;
    *out_edit = edit;
    layout->addWidget(block);
  };

  build_block(tr("Random (Poisson)"), &random_dropouts_group_,
              &random_scale_spin_, &random_seed_check_, &random_seed_edit_);
  build_block(tr("Scratch"), &scratch_dropouts_group_, &scratch_scale_spin_,
              &scratch_seed_check_, &scratch_seed_edit_);
  return group;
}

QWidget* SectionEditor::BuildOsdGroup() {
  osd_group_ = new QGroupBox(tr("On-screen display"), content_);
  osd_group_->setCheckable(true);
  auto* layout = new QVBoxLayout(osd_group_);

  osd_table_ = new QTableWidget(0, kOsdColumnCount, osd_group_);
  osd_table_->setHorizontalHeaderLabels({tr("Text"), tr("X"), tr("Y"),
                                         tr("Scale"), tr("Fg luma"),
                                         tr("Bg luma")});
  osd_table_->horizontalHeader()->setSectionResizeMode(kOsdTextColumn,
                                                       QHeaderView::Stretch);
  osd_table_->verticalHeader()->setVisible(false);
  osd_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  osd_table_->setSelectionMode(QAbstractItemView::SingleSelection);
  layout->addWidget(osd_table_);

  auto* buttons = new QHBoxLayout();
  auto* add_button = new QPushButton(tr("Add overlay"), osd_group_);
  remove_overlay_button_ = new QPushButton(tr("Remove overlay"), osd_group_);
  buttons->addWidget(add_button);
  buttons->addWidget(remove_overlay_button_);
  buttons->addStretch();
  layout->addLayout(buttons);

  QString token_help = tr("Tokens:");
  for (const OsdTokenHelp& help : OsdTokenCatalogue()) {
    token_help += QStringLiteral("\n%1 — %2")
                      .arg(QString::fromStdString(help.token),
                           QString::fromStdString(help.description));
  }
  auto* token_label = new QLabel(token_help, osd_group_);
  token_label->setWordWrap(true);
  layout->addWidget(token_label);

  connect(osd_group_, &QGroupBox::toggled, this, [this](bool checked) {
    if (updating_) {
      return;
    }
    if (checked && osd_table_->rowCount() == 0) {
      // The block exists exactly when overlays exist; seed the first one.
      OnAddOverlay();
      return;
    }
    CommitSection();
  });
  connect(add_button, &QPushButton::clicked, this,
          &SectionEditor::OnAddOverlay);
  connect(remove_overlay_button_, &QPushButton::clicked, this,
          &SectionEditor::OnRemoveOverlay);
  return osd_group_;
}

QWidget* SectionEditor::BuildInjectionsGroup() {
  auto* group = new QGroupBox(tr("Line injections"), content_);
  auto* layout = new QVBoxLayout(group);
  injections_editor_ = new LineInjectionsEditor(group);
  injections_editor_->setMinimumHeight(220);
  layout->addWidget(injections_editor_);

  connect(injections_editor_, &LineInjectionsEditor::InjectionsEdited, this,
          [this] {
            if (!updating_) {
              CommitSection();
            }
          });
  return group;
}

void SectionEditor::SetCurrentSection(int index) {
  section_index_ =
      (index >= 0 && index < document_->section_count()) ? index : -1;
  const bool has_section = section_index_ >= 0;
  placeholder_->setVisible(!has_section);
  content_->setVisible(has_section);
  if (has_section) {
    LoadFromDocument();
    RequestProbe();
  }
}

void SectionEditor::LoadFromDocument() {
  if (section_index_ < 0 || section_index_ >= document_->section_count()) {
    return;
  }
  updating_ = true;
  const Project& project = document_->project();
  const Section& section =
      project.sections[static_cast<std::size_t>(section_index_)];

  name_edit_->setText(QString::fromStdString(section.name));
  section_type_combo_->setCurrentIndex(
      section.section_type == SectionType::kUnknown
          ? 0
          : section_type_combo_->findText(QString::fromStdString(
                SectionTypeToString(section.section_type))));
  LoadSourceWidgets(section.source);
  duration_all_check_->setChecked(section.duration_frames_all);
  duration_spin_->setEnabled(!section.duration_frames_all);
  if (section.duration_frames > 0) {
    duration_spin_->setValue(section.duration_frames);
  }

  const std::vector<SectionListRow> rows = BuildSectionListRows(project);
  start_frame_label_->setText(QStringLiteral("%1").arg(
      rows[static_cast<std::size_t>(section_index_)].start_frame));

  // Audio.
  audio_editor_->SetChannelPairs(section.audio_channel_pairs);

  // Noise.
  noise_group_->setChecked(section.noise.enabled);
  noise_db_spin_->setValue(section.noise.noise_db);
  noise_spread_spin_->setValue(section.noise.noise_spread_db);
  noise_seed_check_->setChecked(section.noise.noise_seed_specified);
  noise_seed_edit_->setEnabled(section.noise.noise_seed_specified);
  noise_seed_edit_->setText(
      section.noise.noise_seed_specified
          ? QString::number(static_cast<qlonglong>(section.noise.noise_seed))
          : QString());

  // Dropouts.
  const DropoutParameters& dropouts = section.dropouts;
  random_dropouts_group_->setChecked(dropouts.random.enabled);
  random_scale_spin_->setValue(
      qMax(editor_limits::kDropoutScaleMin, dropouts.random.scale));
  random_seed_check_->setChecked(dropouts.random.seed_specified);
  random_seed_edit_->setEnabled(dropouts.random.seed_specified);
  random_seed_edit_->setText(
      dropouts.random.seed_specified
          ? QString::number(static_cast<qlonglong>(dropouts.random.seed))
          : QString());
  scratch_dropouts_group_->setChecked(dropouts.scratch.enabled);
  scratch_scale_spin_->setValue(
      qMax(editor_limits::kDropoutScaleMin, dropouts.scratch.scale));
  scratch_seed_check_->setChecked(dropouts.scratch.seed_specified);
  scratch_seed_edit_->setEnabled(dropouts.scratch.seed_specified);
  scratch_seed_edit_->setText(
      dropouts.scratch.seed_specified
          ? QString::number(static_cast<qlonglong>(dropouts.scratch.seed))
          : QString());

  // OSD.
  osd_group_->setChecked(OsdBlockEnabled(section));
  LoadOsdTable(section);

  // Line injections.
  injections_editor_->SetContext(project.cvbs_presets.video_standard_preset,
                                 section.section_type);
  injections_editor_->SetInjections(section.line_injections);

  updating_ = false;
}

void SectionEditor::LoadOsdTable(const Section& section) {
  osd_table_->setRowCount(static_cast<int>(section.osd.overlays.size()));

  const auto commit = [this] {
    if (!updating_) {
      CommitSection();
    }
  };

  for (int row = 0; row < static_cast<int>(section.osd.overlays.size());
       ++row) {
    const OsdOverlay& overlay =
        section.osd.overlays[static_cast<std::size_t>(row)];

    auto* text_edit = new QLineEdit(osd_table_);
    text_edit->setText(QString::fromStdString(overlay.text));
    connect(text_edit, &QLineEdit::editingFinished, this, commit);
    osd_table_->setCellWidget(row, kOsdTextColumn, text_edit);

    const auto make_int_spin = [&](int column, int min, int max, int value) {
      auto* spin = new QSpinBox(osd_table_);
      spin->setRange(min, max);
      spin->setValue(value);
      connect(spin, &QSpinBox::valueChanged, this, commit);
      osd_table_->setCellWidget(row, column, spin);
    };
    make_int_spin(kOsdXColumn, 0, kMaxOverlayOffset, overlay.x);
    make_int_spin(kOsdYColumn, 0, kMaxOverlayOffset, overlay.y);
    make_int_spin(kOsdScaleColumn, editor_limits::kOsdScaleMin,
                  editor_limits::kOsdScaleMax, overlay.scale);

    auto* fg_spin = new QDoubleSpinBox(osd_table_);
    fg_spin->setRange(editor_limits::kLumaMin, editor_limits::kLumaMax);
    fg_spin->setSingleStep(0.05);
    fg_spin->setDecimals(2);
    fg_spin->setValue(overlay.fg_luma);
    connect(fg_spin, &QDoubleSpinBox::valueChanged, this, commit);
    osd_table_->setCellWidget(row, kOsdFgColumn, fg_spin);

    auto* bg_spin = new QDoubleSpinBox(osd_table_);
    // -1 is the "transparent background" sentinel accepted by the validator.
    bg_spin->setRange(-1.0, editor_limits::kLumaMax);
    bg_spin->setSingleStep(0.05);
    bg_spin->setDecimals(2);
    bg_spin->setSpecialValueText(tr("transparent"));
    bg_spin->setValue(overlay.bg_luma);
    connect(bg_spin, &QDoubleSpinBox::valueChanged, this, commit);
    osd_table_->setCellWidget(row, kOsdBgColumn, bg_spin);
  }
  remove_overlay_button_->setEnabled(!section.osd.overlays.empty());
}

Section SectionEditor::SectionFromWidgets() const {
  Section section =
      document_->project().sections[static_cast<std::size_t>(section_index_)];

  section.name = name_edit_->text().toStdString();
  section.section_type =
      section_type_combo_->currentIndex() <= 0
          ? SectionType::kUnknown
          : SectionTypeFromString(
                section_type_combo_->currentText().toStdString());
  section.source = SourceFromWidgets();
  section.duration_frames_all = duration_all_check_->isChecked();
  section.duration_frames =
      section.duration_frames_all ? 0 : duration_spin_->value();

  // Audio block.
  section.audio_channel_pairs = audio_editor_->channel_pairs();

  // Noise block.
  if (noise_group_->isChecked()) {
    section.noise.enabled = true;
    section.noise.noise_db = noise_db_spin_->value();
    section.noise.noise_spread_db = noise_spread_spin_->value();
    section.noise.noise_seed_specified = noise_seed_check_->isChecked();
    section.noise.noise_seed = noise_seed_check_->isChecked()
                                   ? ParseSeed(noise_seed_edit_->text())
                                   : 0;
  } else {
    SetNoiseBlockEnabled(&section, false);
  }

  // Dropout blocks.
  if (random_dropouts_group_->isChecked()) {
    section.dropouts.random.enabled = true;
    section.dropouts.random.scale = random_scale_spin_->value();
    section.dropouts.random.seed_specified = random_seed_check_->isChecked();
    section.dropouts.random.seed = random_seed_check_->isChecked()
                                       ? ParseSeed(random_seed_edit_->text())
                                       : 0;
  } else {
    SetRandomDropoutsEnabled(&section, false);
  }
  if (scratch_dropouts_group_->isChecked()) {
    section.dropouts.scratch.enabled = true;
    section.dropouts.scratch.scale = scratch_scale_spin_->value();
    section.dropouts.scratch.seed_specified = scratch_seed_check_->isChecked();
    section.dropouts.scratch.seed = scratch_seed_check_->isChecked()
                                        ? ParseSeed(scratch_seed_edit_->text())
                                        : 0;
  } else {
    SetScratchDropoutsEnabled(&section, false);
  }

  // OSD block.
  section.osd.overlays.clear();
  if (osd_group_->isChecked()) {
    for (int row = 0; row < osd_table_->rowCount(); ++row) {
      OsdOverlay overlay;
      if (auto* text_edit = qobject_cast<QLineEdit*>(
              osd_table_->cellWidget(row, kOsdTextColumn))) {
        overlay.text = text_edit->text().toStdString();
      }
      const auto int_value = [this, row](int column, int fallback) {
        auto* spin =
            qobject_cast<QSpinBox*>(osd_table_->cellWidget(row, column));
        return spin != nullptr ? spin->value() : fallback;
      };
      const auto double_value = [this, row](int column, double fallback) {
        auto* spin =
            qobject_cast<QDoubleSpinBox*>(osd_table_->cellWidget(row, column));
        return spin != nullptr ? spin->value() : fallback;
      };
      overlay.x = int_value(kOsdXColumn, 0);
      overlay.y = int_value(kOsdYColumn, 0);
      overlay.scale = int_value(kOsdScaleColumn, 1);
      overlay.fg_luma = double_value(kOsdFgColumn, 1.0);
      overlay.bg_luma = double_value(kOsdBgColumn, -1.0);
      section.osd.overlays.push_back(overlay);
    }
  }

  // Line injections.
  section.line_injections = injections_editor_->injections();

  return section;
}

void SectionEditor::CommitSection() {
  if (updating_ || section_index_ < 0) {
    return;
  }
  Section section = SectionFromWidgets();
  committing_ = true;
  const bool changed = document_->SetSection(section_index_, section);
  committing_ = false;

  if (changed) {
    // Refresh derived displays (start frame, block defaults seeded by the
    // enable helpers, injection catalogue context).
    LoadFromDocument();
  }
}

void SectionEditor::RequestProbe() {
  if (section_index_ < 0) {
    return;
  }
  const Project& project = document_->project();
  // Resolve the source path exactly as generation/preview will (asset_bases
  // and, for saved projects, the project file's directory) so the probe reads
  // the same file the pipeline would. anchor_unset mirrors the GUI runner.
  const Project resolved = videosynth::ResolveProjectPaths(
      project, GuiAssetRoots(), ProjectBaseDir().toStdString(),
      /*anchor_unset=*/true);
  probe_controller_->RequestProbe(
      resolved.sections[static_cast<std::size_t>(section_index_)],
      resolved.cvbs_presets.video_standard_preset);
}

void SectionEditor::UpdateProbeDisplay() {
  if (!probe_controller_->has_report()) {
    probe_status_label_->clear();
    probe_detail_label_->clear();
    return;
  }
  const SourceProbeReport& report = probe_controller_->report();
  if (!report.probe_ok) {
    probe_status_label_->setText(tr("Probe failed"));
    probe_detail_label_->setText(report.probe_error);
    return;
  }
  if (report.profile_issues.isEmpty()) {
    probe_status_label_->setText(tr("Source profile OK"));
    probe_detail_label_->setText(FormatSourceProfileSummary(report.profile));
    return;
  }
  probe_status_label_->setText(tr("Source profile incompatible"));
  probe_detail_label_->setText(
      FormatSourceProfileSummary(report.profile) + QStringLiteral("\n") +
      report.profile_issues.join(QStringLiteral("\n")));
}

void SectionEditor::OnBrowseSource() {
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Section Source"), source_edit_->text(),
      tr("Progressive sources (*.mkv *.exr);;All files (*)"));
  if (path.isEmpty()) {
    return;
  }

  // If the chosen file sits under the currently-selected root, keep the root
  // and store the remainder; otherwise fall back to an absolute path.
  const QString root_name = source_root_combo_->currentData().toString();
  QString stored = path;
  if (!root_name.isEmpty()) {
    const QString base = QString::fromStdString(videosynth::ResolveAssetPath(
        "{" + root_name.toStdString() + "}", GuiAssetRoots(),
        ProjectBaseDir().toStdString(), /*anchor_unset=*/true));
    if (!base.isEmpty() && path.startsWith(base + QLatin1Char('/'))) {
      source_edit_->setText(path.mid(base.length() + 1));
      CommitSection();
      RequestProbe();
      UpdateSourceResolvedHint();
      return;
    }
  }
  // Not under the selected root: store the absolute path.
  source_root_combo_->setCurrentIndex(source_root_combo_->findData(QString()));
  source_edit_->setText(stored);
  CommitSection();
  RequestProbe();
  UpdateSourceResolvedHint();
}

std::string SectionEditor::SourceFromWidgets() const {
  const QString root_name = source_root_combo_->currentData().toString();
  QString rest = source_edit_->text();
  if (root_name.isEmpty()) {
    return rest.toStdString();  // Absolute (or verbatim) path.
  }
  while (rest.startsWith(QLatin1Char('/'))) {
    rest.remove(0, 1);
  }
  if (rest.isEmpty()) {
    return "{" + root_name.toStdString() + "}";
  }
  return "{" + root_name.toStdString() + "}/" + rest.toStdString();
}

void SectionEditor::LoadSourceWidgets(const std::string& source) {
  QString root_name;  // empty => Absolute
  QString rest = QString::fromStdString(source);

  if (!source.empty() && source.front() == '{') {
    const std::size_t close = source.find('}');
    const std::string name =
        close == std::string::npos ? "" : source.substr(1, close - 1);
    if (videosynth::IsBuiltinRootName(name)) {
      root_name = QString::fromStdString(name);
      std::string tail =
          close == std::string::npos ? "" : source.substr(close + 1);
      if (!tail.empty() && (tail.front() == '/' || tail.front() == '\\')) {
        tail.erase(0, 1);
      }
      rest = QString::fromStdString(tail);
    }
    // Unknown root token: leave as Absolute so the string round-trips verbatim.
  } else if (source.empty() || !std::filesystem::path(source).is_absolute()) {
    // No token and not absolute: a plain relative path is project-relative.
    root_name = QStringLiteral("project");
  }

  const int index = root_name.isEmpty()
                        ? source_root_combo_->findData(QString())
                        : source_root_combo_->findData(root_name);
  source_root_combo_->setCurrentIndex(index >= 0 ? index : 0);
  source_edit_->setText(rest);
  UpdateSourceResolvedHint();
}

void SectionEditor::UpdateSourceResolvedHint() {
  const std::string resolved = videosynth::ResolveAssetPath(
      SourceFromWidgets(), GuiAssetRoots(), ProjectBaseDir().toStdString(),
      /*anchor_unset=*/true);
  source_resolved_hint_->setText(
      resolved.empty() ? QString()
                       : tr("→ %1").arg(QString::fromStdString(resolved)));
}

QString SectionEditor::ProjectBaseDir() const {
  return document_->file_path().isEmpty()
             ? QDir::currentPath()
             : QFileInfo(document_->file_path()).absolutePath();
}

void SectionEditor::OnAddOverlay() {
  if (section_index_ < 0) {
    return;
  }
  Section section = SectionFromWidgets();
  section.osd.overlays.push_back(MakeDefaultOsdOverlay());
  committing_ = true;
  document_->SetSection(section_index_, section);
  committing_ = false;
  LoadFromDocument();
}

void SectionEditor::OnRemoveOverlay() {
  if (section_index_ < 0) {
    return;
  }
  const int row = osd_table_->currentRow();
  Section section = SectionFromWidgets();
  if (row < 0 || row >= static_cast<int>(section.osd.overlays.size())) {
    return;
  }
  section.osd.overlays.erase(section.osd.overlays.begin() + row);
  committing_ = true;
  document_->SetSection(section_index_, section);
  committing_ = false;
  LoadFromDocument();
}

}  // namespace videosynth::gui
