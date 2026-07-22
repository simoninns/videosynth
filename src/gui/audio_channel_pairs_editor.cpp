/*
 * File:        audio_channel_pairs_editor.cpp
 * Module:      gui
 * Purpose:     Editor for a section's audio channel pairs (up to eight stereo
 *              pairs, each with one shared or two independent test tones)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "audio_channel_pairs_editor.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <utility>

#include "form_field_width.h"
#include "section_block_presenters.h"

namespace videosynth::gui {

namespace {

// Indices of the channel-mode combo box entries.
constexpr int kChannelModeLinked = 0;       // One tone on both channels.
constexpr int kChannelModeIndependent = 1;  // Separate left/right tones.

}  // namespace

AudioChannelPairsEditor::AudioChannelPairsEditor(QWidget* parent)
    : QWidget(parent) {
  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  // Left: channel-pair list with add/remove.
  auto* list_panel = new QVBoxLayout();
  pair_list_ = new QListWidget(this);
  pair_list_->setSelectionMode(QAbstractItemView::SingleSelection);
  list_panel->addWidget(pair_list_);

  auto* list_buttons = new QHBoxLayout();
  add_button_ = new QPushButton(tr("Add pair"), this);
  remove_button_ = new QPushButton(tr("Remove pair"), this);
  list_buttons->addWidget(add_button_);
  list_buttons->addWidget(remove_button_);
  list_buttons->addStretch();
  list_panel->addLayout(list_buttons);
  layout->addLayout(list_panel, 1);

  // Right: per-pair form.
  form_panel_ = new QWidget(this);
  auto* form_layout = new QVBoxLayout(form_panel_);
  form_layout->setContentsMargins(0, 0, 0, 0);

  auto* header_form = new QFormLayout();
  pair_spin_ = new QSpinBox(form_panel_);
  pair_spin_->setRange(0, kMaxAudioChannelPairs - 1);
  pair_spin_->setToolTip(
      tr("Channel-pair number 0–7 (names the _audio_<pair>.wav file)."));
  CapFieldWidthAtSizeHint(pair_spin_);
  header_form->addRow(tr("Channel pair:"), pair_spin_);
  description_edit_ = new QLineEdit(form_panel_);
  description_edit_->setPlaceholderText(tr("e.g. Analogue stereo"));
  // Twice the default hint so longer descriptions stay readable; the platform
  // form layout may otherwise keep the field at its narrow size hint.
  description_edit_->setMinimumWidth(description_edit_->sizeHint().width() * 2);
  header_form->addRow(tr("Description:"), description_edit_);
  channel_mode_ = new QComboBox(form_panel_);
  channel_mode_->addItem(tr("Same tone on both channels"));
  channel_mode_->addItem(tr("Independent left and right"));
  channel_mode_->setToolTip(
      tr("With one tone the left settings apply to both channels; independent "
         "mode edits the left and right tones separately."));
  header_form->addRow(tr("Channels:"), channel_mode_);
  form_layout->addLayout(header_form);

  form_layout->addWidget(
      BuildChannelEditor(tr("Tone (both channels)"), &left_));
  form_layout->addWidget(BuildChannelEditor(tr("Right channel"), &right_));
  right_.group->setVisible(false);
  form_layout->addStretch();
  layout->addWidget(form_panel_, 2);

  const auto commit = [this] { CommitForm(); };
  connect(add_button_, &QPushButton::clicked, this,
          &AudioChannelPairsEditor::OnAdd);
  connect(remove_button_, &QPushButton::clicked, this,
          &AudioChannelPairsEditor::OnRemove);
  connect(pair_list_, &QListWidget::currentRowChanged, this,
          [this](int) { LoadForm(); });
  connect(pair_spin_, &QSpinBox::valueChanged, this,
          [commit](int) { commit(); });
  connect(description_edit_, &QLineEdit::editingFinished, this, commit);
  // activated (not currentIndexChanged) so programmatic LoadForm updates do
  // not re-enter the mode switch.
  connect(channel_mode_, &QComboBox::activated, this,
          &AudioChannelPairsEditor::OnChannelModeChanged);

  LoadForm();
}

QWidget* AudioChannelPairsEditor::BuildChannelEditor(const QString& title,
                                                     ChannelWidgets* widgets) {
  const auto commit = [this] { CommitForm(); };

  auto* group = new QGroupBox(title, form_panel_);
  group->setCheckable(true);
  widgets->group = group;
  auto* form = new QFormLayout(group);

  widgets->waveform = new QComboBox(group);
  for (const std::string& waveform : AudioWaveformOptions()) {
    widgets->waveform->addItem(QString::fromStdString(waveform));
  }
  form->addRow(tr("Waveform:"), widgets->waveform);

  widgets->frequency = new QDoubleSpinBox(group);
  widgets->frequency->setRange(editor_limits::kAudioFrequencyMinHz,
                               editor_limits::kAudioFrequencyMaxHz);
  widgets->frequency->setSuffix(tr(" Hz"));
  widgets->frequency->setDecimals(1);
  widgets->frequency->setValue(1000.0);
  CapFieldWidthAtSizeHint(widgets->frequency);
  form->addRow(tr("Frequency:"), widgets->frequency);

  widgets->amplitude = new QDoubleSpinBox(group);
  widgets->amplitude->setRange(editor_limits::kAudioAmplitudeMin,
                               editor_limits::kAudioAmplitudeMax);
  widgets->amplitude->setSingleStep(0.05);
  widgets->amplitude->setDecimals(2);
  widgets->amplitude->setValue(0.5);
  CapFieldWidthAtSizeHint(widgets->amplitude);
  form->addRow(tr("Amplitude:"), widgets->amplitude);

  widgets->ramp_group = new QGroupBox(tr("Frequency ramp"), group);
  widgets->ramp_group->setCheckable(true);
  widgets->ramp_group->setChecked(false);
  auto* ramp_form = new QFormLayout(widgets->ramp_group);
  widgets->ramp_start = new QDoubleSpinBox(widgets->ramp_group);
  widgets->ramp_start->setRange(editor_limits::kAudioFrequencyMinHz,
                                editor_limits::kAudioFrequencyMaxHz);
  widgets->ramp_start->setSuffix(tr(" Hz"));
  widgets->ramp_start->setDecimals(1);
  widgets->ramp_end = new QDoubleSpinBox(widgets->ramp_group);
  widgets->ramp_end->setRange(editor_limits::kAudioFrequencyMinHz,
                              editor_limits::kAudioFrequencyMaxHz);
  widgets->ramp_end->setSuffix(tr(" Hz"));
  widgets->ramp_end->setDecimals(1);
  widgets->ramp_mode = new QComboBox(widgets->ramp_group);
  for (const std::string& mode : AudioRampModeOptions()) {
    widgets->ramp_mode->addItem(QString::fromStdString(mode));
  }
  widgets->ramp_period = new QDoubleSpinBox(widgets->ramp_group);
  widgets->ramp_period->setRange(0.0, 86400.0);
  widgets->ramp_period->setDecimals(3);
  widgets->ramp_period->setSuffix(tr(" s"));
  widgets->ramp_period->setSpecialValueText(tr("whole section"));
  CapFieldWidthAtSizeHint(widgets->ramp_start);
  CapFieldWidthAtSizeHint(widgets->ramp_end);
  CapFieldWidthAtSizeHint(widgets->ramp_period);
  ramp_form->addRow(tr("Start:"), widgets->ramp_start);
  ramp_form->addRow(tr("End:"), widgets->ramp_end);
  ramp_form->addRow(tr("Mode:"), widgets->ramp_mode);
  ramp_form->addRow(tr("Period:"), widgets->ramp_period);
  form->addRow(widgets->ramp_group);

  connect(group, &QGroupBox::toggled, this, [commit](bool) { commit(); });
  connect(widgets->ramp_group, &QGroupBox::toggled, this,
          [this, widgets](bool checked) {
            if (updating_) {
              return;
            }
            if (checked && widgets->ramp_start->value() == 0.0 &&
                widgets->ramp_end->value() == 0.0) {
              // Seed an audible sweep instead of a degenerate 0→0 Hz ramp.
              updating_ = true;
              widgets->ramp_start->setValue(200.0);
              widgets->ramp_end->setValue(4000.0);
              updating_ = false;
            }
            CommitForm();
          });
  connect(widgets->waveform, &QComboBox::activated, this,
          [commit](int) { commit(); });
  connect(widgets->frequency, &QDoubleSpinBox::valueChanged, this,
          [commit](double) { commit(); });
  connect(widgets->amplitude, &QDoubleSpinBox::valueChanged, this,
          [commit](double) { commit(); });
  connect(widgets->ramp_start, &QDoubleSpinBox::valueChanged, this,
          [commit](double) { commit(); });
  connect(widgets->ramp_end, &QDoubleSpinBox::valueChanged, this,
          [commit](double) { commit(); });
  connect(widgets->ramp_mode, &QComboBox::activated, this,
          [commit](int) { commit(); });
  connect(widgets->ramp_period, &QDoubleSpinBox::valueChanged, this,
          [commit](double) { commit(); });
  return group;
}

void AudioChannelPairsEditor::ApplyChannelMode(bool independent) {
  left_.group->setTitle(independent ? tr("Left channel")
                                    : tr("Tone (both channels)"));
  right_.group->setVisible(independent);
}

void AudioChannelPairsEditor::OnChannelModeChanged(int index) {
  const bool independent = index == kChannelModeIndependent;
  if (independent) {
    // Seed the right channel from the left so splitting starts from a copy
    // instead of forcing the user to re-enter matching settings.
    updating_ = true;
    LoadChannel(right_, ChannelFromWidgets(left_));
    updating_ = false;
  }
  ApplyChannelMode(independent);
  CommitForm();
}

void AudioChannelPairsEditor::SetChannelPairs(
    std::vector<AudioChannelPair> pairs) {
  // Preserve the selected row across a refresh. Editing a channel round-trips
  // through the owning section editor, which calls this to reload; resetting to
  // row 0 would yank the user off the pair they are editing. Pairs never
  // reorder on edit, so the row index stays valid (clamped for a shorter list).
  const int previous_row = pair_list_->currentRow();
  pairs_ = std::move(pairs);
  const int select_row =
      pairs_.empty()
          ? -1
          : qBound(0, previous_row, static_cast<int>(pairs_.size()) - 1);
  RebuildList(select_row);
}

void AudioChannelPairsEditor::RebuildList(int select_row) {
  updating_ = true;
  pair_list_->clear();
  for (const AudioChannelPair& channel_pair : pairs_) {
    pair_list_->addItem(PairSummary(channel_pair));
  }
  updating_ = false;
  pair_list_->setCurrentRow(qBound(-1, select_row, pair_list_->count() - 1));
  LoadForm();
}

AudioChannelPair* AudioChannelPairsEditor::Current() {
  const int row = pair_list_->currentRow();
  if (row < 0 || row >= static_cast<int>(pairs_.size())) {
    return nullptr;
  }
  return &pairs_[static_cast<std::size_t>(row)];
}

void AudioChannelPairsEditor::LoadChannel(const ChannelWidgets& widgets,
                                          const AudioParameters& audio) {
  widgets.group->setChecked(audio.enabled);
  widgets.waveform->setCurrentIndex(
      qMax(0, widgets.waveform->findText(
                  audio.waveform_text.empty()
                      ? QStringLiteral("sine")
                      : QString::fromStdString(audio.waveform_text))));
  widgets.frequency->setValue(audio.frequency_hz);
  widgets.amplitude->setValue(audio.amplitude);
  widgets.ramp_group->setChecked(audio.ramp_enabled);
  widgets.ramp_start->setValue(audio.ramp_start_hz);
  widgets.ramp_end->setValue(audio.ramp_end_hz);
  widgets.ramp_mode->setCurrentIndex(
      qMax(0, widgets.ramp_mode->findText(
                  audio.ramp_mode_text.empty()
                      ? QStringLiteral("up")
                      : QString::fromStdString(audio.ramp_mode_text))));
  widgets.ramp_period->setValue(audio.ramp_period_seconds);
}

void AudioChannelPairsEditor::LoadForm() {
  updating_ = true;
  AudioChannelPair* channel_pair = Current();
  form_panel_->setEnabled(channel_pair != nullptr);
  remove_button_->setEnabled(channel_pair != nullptr);
  add_button_->setEnabled(static_cast<int>(pairs_.size()) <
                          kMaxAudioChannelPairs);

  if (channel_pair == nullptr) {
    updating_ = false;
    return;
  }

  pair_spin_->setValue(channel_pair->pair);
  description_edit_->setText(QString::fromStdString(channel_pair->description));
  // Linked mode is inferred, not stored: identical left/right descriptors edit
  // as one tone; any difference (including one silent channel) edits as
  // independent channels.
  const bool independent = !(channel_pair->left == channel_pair->right);
  channel_mode_->setCurrentIndex(independent ? kChannelModeIndependent
                                             : kChannelModeLinked);
  ApplyChannelMode(independent);
  LoadChannel(left_, channel_pair->left);
  LoadChannel(right_, channel_pair->right);
  updating_ = false;
}

AudioParameters AudioChannelPairsEditor::ChannelFromWidgets(
    const ChannelWidgets& widgets) const {
  AudioParameters audio;
  if (!widgets.group->isChecked()) {
    return audio;  // Disabled (silent) channel.
  }
  audio.enabled = true;
  const std::string waveform = widgets.waveform->currentText().toStdString();
  audio.waveform = AudioWaveformFromString(waveform);
  audio.waveform_text = waveform;
  audio.frequency_hz = widgets.frequency->value();
  audio.amplitude = widgets.amplitude->value();
  if (widgets.ramp_group->isChecked()) {
    audio.ramp_enabled = true;
    audio.ramp_start_hz = widgets.ramp_start->value();
    audio.ramp_end_hz = widgets.ramp_end->value();
    audio.ramp_start_specified = true;
    audio.ramp_end_specified = true;
    const std::string mode = widgets.ramp_mode->currentText().toStdString();
    audio.ramp_mode = AudioRampModeFromString(mode);
    audio.ramp_mode_text = mode;
    audio.ramp_period_seconds = widgets.ramp_period->value();
  }
  return audio;
}

void AudioChannelPairsEditor::CommitForm() {
  if (updating_) {
    return;
  }
  AudioChannelPair* channel_pair = Current();
  if (channel_pair == nullptr) {
    return;
  }
  channel_pair->pair = pair_spin_->value();
  channel_pair->pair_specified = true;
  channel_pair->description = description_edit_->text().toStdString();
  channel_pair->left = ChannelFromWidgets(left_);
  channel_pair->right = channel_mode_->currentIndex() == kChannelModeIndependent
                            ? ChannelFromWidgets(right_)
                            : channel_pair->left;

  if (QListWidgetItem* item = pair_list_->currentItem()) {
    item->setText(PairSummary(*channel_pair));
  }
  emit ChannelPairsEdited();
}

void AudioChannelPairsEditor::OnAdd() {
  const int next = NextFreeAudioChannelPair(pairs_);
  if (next < 0) {
    return;  // All eight channel pairs already in use.
  }
  pairs_.push_back(MakeDefaultAudioChannelPair(next));
  RebuildList(static_cast<int>(pairs_.size()) - 1);
  emit ChannelPairsEdited();
}

void AudioChannelPairsEditor::OnRemove() {
  const int row = pair_list_->currentRow();
  if (row < 0 || row >= static_cast<int>(pairs_.size())) {
    return;
  }
  pairs_.erase(pairs_.begin() + row);
  RebuildList(qMin(row, static_cast<int>(pairs_.size()) - 1));
  emit ChannelPairsEdited();
}

QString AudioChannelPairsEditor::PairSummary(
    const AudioChannelPair& channel_pair) const {
  QString summary = tr("Pair %1").arg(channel_pair.pair);
  if (!channel_pair.description.empty()) {
    summary += QStringLiteral(" — %1").arg(
        QString::fromStdString(channel_pair.description));
  }
  QString channels;
  if (channel_pair.left.enabled) {
    channels += QStringLiteral("L");
  }
  if (channel_pair.right.enabled) {
    channels += QStringLiteral("R");
  }
  summary +=
      QStringLiteral(" [%1]").arg(channels.isEmpty() ? tr("silent") : channels);
  return summary;
}

}  // namespace videosynth::gui
