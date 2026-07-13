/*
 * File:        log_message_model.h
 * Module:      gui
 * Purpose:     Bounded list model of pipeline log messages for the log dock
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <QAbstractListModel>
#include <QString>
#include <deque>

#include "forwarding_logger.h"

namespace videosynth::gui {

// One captured log line presented in the log dock.
struct LogEntry {
  LogSeverity severity = LogSeverity::kInfo;
  QString message;
};

// Read-only, bounded list model over captured pipeline log messages. When
// the entry limit is reached the oldest entry is evicted, so memory stays
// constant over arbitrarily long runs. Severity foreground colours are
// theme-aware (theme_color_tokens.h).
//
// Thread-safety: NOT thread-safe. GUI (owning) thread only; worker log
// lines must be marshalled (queued signal) before calling Append.
class LogMessageModel : public QAbstractListModel {
  Q_OBJECT

 public:
  enum Roles {
    kSeverityRole = Qt::UserRole + 1,  // int(LogSeverity)
  };

  static constexpr int kDefaultMaxEntries = 5000;

  explicit LogMessageModel(int max_entries = kDefaultMaxEntries,
                           QObject* parent = nullptr);

  // Default argument mirrors the QAbstractItemModel base signature.
  // NOLINTNEXTLINE(google-default-arguments)
  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role) const override;

  // Appends one entry, evicting the oldest when the bound is reached.
  void Append(LogSeverity severity, const QString& message);
  void Clear();

  int max_entries() const { return max_entries_; }

  // Selects the theme variant of the severity foreground colours.
  void SetDarkTheme(bool dark_theme);

 private:
  std::deque<LogEntry> entries_;
  int max_entries_;
  bool dark_theme_ = false;
};

}  // namespace videosynth::gui
