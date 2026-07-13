/*
 * File:        log_message_model.cpp
 * Module:      gui
 * Purpose:     Bounded list model of pipeline log messages for the log dock
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "log_message_model.h"

#include <QBrush>
#include <QColor>
#include <algorithm>

#include "theme_color_tokens.h"

namespace videosynth::gui {

namespace {

// Foreground colour per severity; an invalid colour means "use the view's
// default text colour" (info messages).
QColor SeverityColor(LogSeverity severity, bool dark_theme) {
  switch (severity) {
    case LogSeverity::kError:
      return theme_tokens::IssueSeverityColor(true, dark_theme);
    case LogSeverity::kWarning:
      return theme_tokens::IssueSeverityColor(false, dark_theme);
    case LogSeverity::kDebug:
    case LogSeverity::kTrace:
      return dark_theme ? QColor(150, 150, 150) : QColor(120, 120, 120);
    case LogSeverity::kInfo:
      break;
  }
  return {};
}

}  // namespace

LogMessageModel::LogMessageModel(int max_entries, QObject* parent)
    : QAbstractListModel(parent), max_entries_(std::max(1, max_entries)) {}

// NOLINTNEXTLINE(google-default-arguments): base-signature default argument
int LogMessageModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return static_cast<int>(entries_.size());
}

QVariant LogMessageModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 ||
      index.row() >= static_cast<int>(entries_.size())) {
    return {};
  }

  const LogEntry& entry = entries_[static_cast<std::size_t>(index.row())];
  switch (role) {
    case Qt::DisplayRole:
      return entry.message;
    case kSeverityRole:
      return static_cast<int>(entry.severity);
    case Qt::ForegroundRole: {
      const QColor color = SeverityColor(entry.severity, dark_theme_);
      if (!color.isValid()) {
        return {};
      }
      return QBrush(color);
    }
    default:
      return {};
  }
}

void LogMessageModel::Append(LogSeverity severity, const QString& message) {
  if (static_cast<int>(entries_.size()) >= max_entries_) {
    beginRemoveRows(QModelIndex(), 0, 0);
    entries_.pop_front();
    endRemoveRows();
  }

  const int row = static_cast<int>(entries_.size());
  beginInsertRows(QModelIndex(), row, row);
  entries_.push_back(LogEntry{severity, message});
  endInsertRows();
}

void LogMessageModel::Clear() {
  if (entries_.empty()) {
    return;
  }
  beginResetModel();
  entries_.clear();
  endResetModel();
}

void LogMessageModel::SetDarkTheme(bool dark_theme) {
  if (dark_theme_ == dark_theme) {
    return;
  }
  dark_theme_ = dark_theme;
  if (!entries_.empty()) {
    emit dataChanged(index(0), index(static_cast<int>(entries_.size()) - 1),
                     {Qt::ForegroundRole});
  }
}

}  // namespace videosynth::gui
