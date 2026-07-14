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
