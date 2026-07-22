/*
 * File:        audio_channel_pairs_editor.h
 * Module:      gui
 * Purpose:     Editor for a section's audio channel pairs (up to eight stereo
 *              pairs, each with one shared or two independent test tones)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <QWidget>
#include <vector>

#include "videosynth/model.h"

class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;

namespace videosynth::gui {

// Edits a working copy of a section's audio channel-pair list. Each pair names
// a channel-pair number 0–7 and carries left/right tone descriptors; an
// unchecked channel is stored silent (all zeros) per the CVBS File Format
// Specification (Audio Data). A pair is edited either as one tone applied to
// both channels (the default) or as independent left/right tones; the mode is
// purely a GUI convenience inferred from the model (left == right → linked),
// so the stored YAML format is unchanged. The owner reads back channel_pairs()
// and commits after every ChannelPairsEdited signal.
//
// Thread-safety: NOT thread-safe. GUI (main) thread only.
class AudioChannelPairsEditor : public QWidget {
  Q_OBJECT

 public:
  explicit AudioChannelPairsEditor(QWidget* parent = nullptr);

  // Replaces the working copy (no ChannelPairsEdited emission).
  void SetChannelPairs(std::vector<AudioChannelPair> pairs);

  const std::vector<AudioChannelPair>& channel_pairs() const { return pairs_; }

 signals:
  // The working copy changed through user interaction.
  void ChannelPairsEdited();

 private:
  // The widgets making up one channel (left or right) tone sub-editor.
  struct ChannelWidgets {
    QGroupBox* group = nullptr;  // checkable == channel active
    QComboBox* waveform = nullptr;
    QDoubleSpinBox* frequency = nullptr;
    QDoubleSpinBox* amplitude = nullptr;
    QGroupBox* ramp_group = nullptr;  // checkable == ramp enabled
    QDoubleSpinBox* ramp_start = nullptr;
    QDoubleSpinBox* ramp_end = nullptr;
    QComboBox* ramp_mode = nullptr;
    QDoubleSpinBox* ramp_period = nullptr;
  };

  QWidget* BuildChannelEditor(const QString& title, ChannelWidgets* widgets);
  // Retitles the left editor and shows/hides the right editor for the mode.
  void ApplyChannelMode(bool independent);
  void OnChannelModeChanged(int index);
  void RebuildList(int select_row);
  void LoadForm();
  void CommitForm();
  AudioChannelPair* Current();

  void LoadChannel(const ChannelWidgets& widgets, const AudioParameters& audio);
  AudioParameters ChannelFromWidgets(const ChannelWidgets& widgets) const;

  void OnAdd();
  void OnRemove();

  QString PairSummary(const AudioChannelPair& channel_pair) const;

  std::vector<AudioChannelPair> pairs_;
  bool updating_ = false;

  QListWidget* pair_list_ = nullptr;
  QPushButton* add_button_ = nullptr;
  QPushButton* remove_button_ = nullptr;
  QWidget* form_panel_ = nullptr;
  QSpinBox* pair_spin_ = nullptr;
  QLineEdit* description_edit_ = nullptr;
  QComboBox* channel_mode_ = nullptr;
  ChannelWidgets left_;
  ChannelWidgets right_;
};

}  // namespace videosynth::gui
