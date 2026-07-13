/*
 * File:        welcome_page.h
 * Module:      gui
 * Purpose:     Empty-state landing surface shown when no project is open
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <QString>
#include <QWidget>

class QListWidget;

namespace videosynth::gui {

// Landing surface shown in the central area while no project is open: the
// application logo, New Project / Open Project actions, and a recent-files
// list. Purely a launcher — it holds no project state and emits requests the
// main window fulfils.
//
// Thread-safety: NOT thread-safe. GUI (main) thread only.
class WelcomePage : public QWidget {
  Q_OBJECT

 public:
  explicit WelcomePage(QWidget* parent = nullptr);

  // Repopulates the recent-files list from the given absolute paths.
  void SetRecentFiles(const QStringList& paths);

 signals:
  void NewProjectRequested();
  void OpenProjectRequested();
  void RecentFileRequested(const QString& path);

 private:
  QListWidget* recent_list_ = nullptr;
};

}  // namespace videosynth::gui
