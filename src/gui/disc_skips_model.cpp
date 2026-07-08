/*
 * File:        disc_skips_model.cpp
 * Module:      gui
 * Purpose:     Editable table model of project-level disc skip events
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "disc_skips_model.h"

#include <utility>

namespace videosynth::gui {

namespace {

QString DirectionText(DiscSkipDirection direction) {
  return direction == DiscSkipDirection::kForward ? QStringLiteral("forward")
                                                  : QStringLiteral("backward");
}

}  // namespace

DiscSkipsModel::DiscSkipsModel(ProjectDocument* document, QObject* parent)
    : QAbstractTableModel(parent), document_(document) {
  skips_ = document_->project().disc_skips;

  const auto reload = [this] {
    if (!committing_) {
      Reload();
    }
  };
  connect(document_, &ProjectDocument::DocumentReset, this, reload);
  connect(document_, &ProjectDocument::DiscSkipsChanged, this, reload);
}

// NOLINTNEXTLINE(google-default-arguments): base-signature default argument
int DiscSkipsModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(skips_.size());
}

// NOLINTNEXTLINE(google-default-arguments): base-signature default argument
int DiscSkipsModel::columnCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : kColumnCount;
}

QVariant DiscSkipsModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 ||
      index.row() >= static_cast<int>(skips_.size())) {
    return {};
  }
  if (role != Qt::DisplayRole && role != Qt::EditRole) {
    return {};
  }

  const DiscSkip& skip = skips_[static_cast<std::size_t>(index.row())];
  switch (index.column()) {
    case kAtFrameColumn:
      return skip.at_frame;
    case kDirectionColumn:
      return DirectionText(skip.direction);
    case kCountColumn:
      return skip.count;
    default:
      return {};
  }
}

QVariant DiscSkipsModel::headerData(int section, Qt::Orientation orientation,
                                    int role) const {
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
    return {};
  }
  switch (section) {
    case kAtFrameColumn:
      return tr("At frame");
    case kDirectionColumn:
      return tr("Direction");
    case kCountColumn:
      return tr("Count");
    default:
      return {};
  }
}

Qt::ItemFlags DiscSkipsModel::flags(const QModelIndex& index) const {
  if (!index.isValid()) {
    return Qt::NoItemFlags;
  }
  return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

bool DiscSkipsModel::setData(const QModelIndex& index, const QVariant& value,
                             int role) {
  if (role != Qt::EditRole || !index.isValid() || index.row() < 0 ||
      index.row() >= static_cast<int>(skips_.size())) {
    return false;
  }

  std::vector<DiscSkip> updated = skips_;
  DiscSkip& skip = updated[static_cast<std::size_t>(index.row())];
  switch (index.column()) {
    case kAtFrameColumn: {
      bool ok = false;
      const int at_frame = value.toInt(&ok);
      if (!ok) {
        return false;
      }
      skip.at_frame = at_frame;
      break;
    }
    case kDirectionColumn: {
      const QString text = value.toString().trimmed().toLower();
      if (text == QStringLiteral("forward")) {
        skip.direction = DiscSkipDirection::kForward;
      } else if (text == QStringLiteral("backward")) {
        skip.direction = DiscSkipDirection::kBackward;
      } else {
        return false;
      }
      break;
    }
    case kCountColumn: {
      bool ok = false;
      const int count = value.toInt(&ok);
      if (!ok) {
        return false;
      }
      skip.count = count;
      break;
    }
    default:
      return false;
  }

  if (!Commit(std::move(updated))) {
    return false;
  }
  emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
  return true;
}

void DiscSkipsModel::AddSkip() {
  std::vector<DiscSkip> updated = skips_;
  DiscSkip skip;
  skip.at_frame = 1;
  skip.direction = DiscSkipDirection::kForward;
  skip.count = 1;
  updated.push_back(skip);

  const int row = static_cast<int>(skips_.size());
  beginInsertRows(QModelIndex(), row, row);
  Commit(std::move(updated));
  endInsertRows();
}

void DiscSkipsModel::RemoveSkip(int row) {
  if (row < 0 || row >= static_cast<int>(skips_.size())) {
    return;
  }
  std::vector<DiscSkip> updated = skips_;
  updated.erase(updated.begin() + row);

  beginRemoveRows(QModelIndex(), row, row);
  Commit(std::move(updated));
  endRemoveRows();
}

void DiscSkipsModel::Reload() {
  beginResetModel();
  skips_ = document_->project().disc_skips;
  endResetModel();
}

bool DiscSkipsModel::Commit(std::vector<DiscSkip> skips) {
  committing_ = true;
  document_->SetDiscSkips(skips);
  committing_ = false;
  skips_ = std::move(skips);
  return true;
}

}  // namespace videosynth::gui
