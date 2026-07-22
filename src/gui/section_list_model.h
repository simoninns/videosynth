/*
 * File:        section_list_model.h
 * Module:      gui
 * Purpose:     Table model of project sections for the sections dock
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <QAbstractTableModel>
#include <QString>
#include <vector>

#include "project_document.h"
#include "videosynth/biphase_types.h"
#include "videosynth/model.h"

namespace videosynth::gui {

// Presentation row for one section: display strings plus the automatically
// recalculated start frame (cumulative sum of the preceding sections'
// durations, matching the pipeline's frame schedule).
struct SectionListRow {
  QString name;
  QString type;
  QString source;
  int start_frame = 0;
  int duration_frames = 0;
  bool duration_all = false;
  int duration_repeat = 1;

  // "500 frames", "all frames", or "all frames x3".
  QString DurationText() const;
};

// Builds the presentation rows for a project, recalculating start frames
// from the ordered section durations.
//
// Thread-safety: thread-safe (pure function).
std::vector<SectionListRow> BuildSectionListRows(const Project& project);

// Formats a section's 0-based output frame span as "start – end". An
// `end_frame` below `start_frame` means the end is not yet known (an "all
// source frames" duration awaiting a probe) and renders as "start – ?".
//
// Thread-safety: thread-safe (pure function).
QString FrameRangeText(int start_frame, int end_frame);

// Row label for the disc position range shown under the frame range:
// "CAV picture numbers:" or "CLV timecode:". Empty for a non-laserdisc
// project (DiscType::kUnknown).
//
// Thread-safety: thread-safe (pure function).
QString DiscRangeTitle(DiscType disc_type);

// Per-section 0-based disc-position frame offsets: how far each section's
// first frame sits into the CAV picture-number / CLV timecode sequence.
// IEC 60856/60857: picture numbers and programme timecodes exist in the
// programme area only, so numbering anchors at the first programme_area
// section (lead_in frames are not counted) and lead_in / lead_out / untyped
// sections yield -1 (no disc position). Once numbering has started the
// count keeps running across every later section — mirroring the generation
// engine, whose timekeeping generators persist across section boundaries —
// but only programme_area sections receive an offset. A CAV programme_area
// section whose picture_number injection code specifies an explicit
// start_value re-anchors the count there. Open-ended "all source frames"
// durations contribute no frames, matching BuildSectionListRows'
// open-ended treatment. All -1 for a non-laserdisc project.
//
// Thread-safety: thread-safe (pure function).
std::vector<int> BuildDiscFrameOffsets(const Project& project,
                                       DiscType disc_type);

// Formats a section's disc-position span (0-based frame offsets from
// BuildDiscFrameOffsets). CAV: five-digit picture numbers, offset 0 =
// picture 00001 ("00001 – 00250"). CLV: programme timecodes "HH:MM:SS:FF"
// at the standard's nominal CLV rate (PAL: 25 fps, NTSC: 30 fps), offset
// 0 = 00:00:00:00. An `end_offset` below `start_offset` renders the end as
// "?". Empty for a non-laserdisc project.
//
// Thread-safety: thread-safe (pure function).
QString DiscRangeText(DiscType disc_type, Standard standard, int start_offset,
                      int end_offset);

// Read-only table model over the document's ordered section list. Stays in
// sync with the document by listening to its granular change signals; edits
// flow the other way through ProjectDocument commands (see SectionListDock).
//
// Thread-safety: NOT thread-safe. GUI (owning) thread only.
class SectionListModel : public QAbstractTableModel {
  Q_OBJECT

 public:
  enum Column {
    kNameColumn = 0,
    kTypeColumn,
    kSourceColumn,
    kStartFrameColumn,
    kDurationColumn,
    kColumnCount,
  };

  // Does not take ownership of `document`; it must outlive the model.
  explicit SectionListModel(ProjectDocument* document,
                            QObject* parent = nullptr);

  // Default arguments mirror the QAbstractItemModel base signatures.
  // NOLINTNEXTLINE(google-default-arguments)
  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  // NOLINTNEXTLINE(google-default-arguments)
  int columnCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role) const override;

 private:
  // Structural change (add/remove/move/reset): rebuild rows and reset the
  // model. Resets clear the view's selection, so this is reserved for changes
  // that alter the row set.
  void Reload();
  // In-place edit of an existing section: rebuild rows and emit dataChanged
  // without a model reset, so the view keeps its current selection. The row
  // count is unchanged by a section edit (only its cells, and the derived
  // start frames of the rows below it).
  void RefreshRows();

  ProjectDocument* document_;
  std::vector<SectionListRow> rows_;
};

}  // namespace videosynth::gui
