/*
 * File:        section_list_model.cpp
 * Module:      gui
 * Purpose:     Table model of project sections for the sections dock
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "section_list_model.h"

namespace videosynth::gui {

QString SectionListRow::DurationText() const {
  if (duration_all) {
    return duration_repeat > 1
               ? QStringLiteral("all frames x%1").arg(duration_repeat)
               : QStringLiteral("all frames");
  }
  return QStringLiteral("%1 frames").arg(duration_frames);
}

std::vector<SectionListRow> BuildSectionListRows(const Project& project) {
  std::vector<SectionListRow> rows;
  rows.reserve(project.sections.size());

  int next_start_frame = 0;
  for (const Section& section : project.sections) {
    SectionListRow row;
    row.name = QString::fromStdString(section.name);
    row.type = QString::fromStdString(section.type);
    row.source = QString::fromStdString(section.source);
    row.start_frame = next_start_frame;
    row.duration_frames = section.duration_frames;
    row.duration_all = section.duration_frames_all;
    row.duration_repeat = section.duration_frames_repeat;
    rows.push_back(row);

    // 'all' resolves to the probed source length at generation time; the
    // recalculated display treats it as open-ended (subsequent sections show
    // the frames accumulated so far).
    if (!section.duration_frames_all) {
      next_start_frame += section.duration_frames;
    }
  }
  return rows;
}

SectionListModel::SectionListModel(ProjectDocument* document, QObject* parent)
    : QAbstractTableModel(parent), document_(document) {
  rows_ = BuildSectionListRows(document_->project());

  const auto reload = [this] { Reload(); };
  connect(document_, &ProjectDocument::DocumentReset, this, reload);
  connect(document_, &ProjectDocument::SectionAdded, this, reload);
  connect(document_, &ProjectDocument::SectionRemoved, this, reload);
  connect(document_, &ProjectDocument::SectionMoved, this, reload);
  // An edit changes cells but not the row set, so refresh in place to keep the
  // dock's selection (a reset would deselect and close the section editor).
  connect(document_, &ProjectDocument::SectionEdited, this,
          [this] { RefreshRows(); });
}

// NOLINTNEXTLINE(google-default-arguments): base-signature default argument
int SectionListModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

// NOLINTNEXTLINE(google-default-arguments): base-signature default argument
int SectionListModel::columnCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : kColumnCount;
}

QVariant SectionListModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 ||
      index.row() >= static_cast<int>(rows_.size())) {
    return {};
  }
  if (role != Qt::DisplayRole && role != Qt::ToolTipRole) {
    return {};
  }

  const SectionListRow& row = rows_[static_cast<std::size_t>(index.row())];
  switch (index.column()) {
    case kNameColumn:
      return row.name;
    case kTypeColumn:
      return row.type;
    case kSourceColumn:
      return row.source;
    case kStartFrameColumn:
      return row.start_frame;
    case kDurationColumn:
      return row.DurationText();
    default:
      return {};
  }
}

QVariant SectionListModel::headerData(int section, Qt::Orientation orientation,
                                      int role) const {
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
    return {};
  }
  switch (section) {
    case kNameColumn:
      return tr("Name");
    case kTypeColumn:
      return tr("Type");
    case kSourceColumn:
      return tr("Source");
    case kStartFrameColumn:
      return tr("Start");
    case kDurationColumn:
      return tr("Duration");
    default:
      return {};
  }
}

void SectionListModel::Reload() {
  beginResetModel();
  rows_ = BuildSectionListRows(document_->project());
  endResetModel();
}

void SectionListModel::RefreshRows() {
  std::vector<SectionListRow> rebuilt =
      BuildSectionListRows(document_->project());
  // A section edit cannot change the row count; if it somehow does, fall back
  // to a full reset to keep the model and view structurally consistent.
  if (rebuilt.size() != rows_.size()) {
    Reload();
    return;
  }
  rows_ = std::move(rebuilt);
  if (rows_.empty()) {
    return;
  }
  emit dataChanged(index(0, 0), index(rowCount() - 1, kColumnCount - 1));
}

}  // namespace videosynth::gui
