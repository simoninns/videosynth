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
    return QStringLiteral("all frames");
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
  connect(document_, &ProjectDocument::SectionEdited, this, reload);
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

}  // namespace videosynth::gui
