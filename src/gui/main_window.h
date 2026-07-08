/*
 * File:        main_window.h
 * Module:      gui
 * Purpose:     Application main window shell with menus and About dialog
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <QCloseEvent>
#include <QMainWindow>
#include <QModelIndex>

#include "project_document.h"
#include "theme_controller.h"
#include "validation_controller.h"
#include "validation_issues_model.h"

class QLabel;
class QMenu;

namespace videosynth::gui {

// Main window shell: menu bar (File / Edit / Project / Generate / View /
// Help), status bar, issues dock, and project file lifecycle (New / Open /
// Save / Save As / Recent Files) over a ProjectDocument. Section editing,
// generation, and preview panes attach here in later phases.
//
// Thread-safety: NOT thread-safe. GUI (main) thread only.
class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  // Does not take ownership of `theme_controller`; it must outlive the
  // window.
  explicit MainWindow(ThemeController* theme_controller,
                      QWidget* parent = nullptr);

  ProjectDocument* document() { return document_; }

 signals:
  // Issue-activation hook: emitted when the user double-clicks an issue in
  // the dock. Section editors (later phase) connect here to focus the
  // offending editor; -1 means a project-level issue.
  void IssueNavigationRequested(int section_index);

 protected:
  void closeEvent(QCloseEvent* event) override;

 private slots:
  void OnAbout();
  void OnNewProject();
  void OnOpenProject();
  bool OnSave();
  bool OnSaveAs();
  void OnIssueActivated(const QModelIndex& index);

 private:
  void BuildMenus();
  void BuildCentralPlaceholder();
  void BuildIssuesDock();
  void RestoreWindowGeometry();

  // Returns false when the user cancels an unsaved-changes prompt.
  bool MaybeSave();
  bool SaveToPath(const QString& path);
  void LoadProjectFromFile(const QString& path);
  void UpdateWindowTitle();
  void UpdateValidationStatus();
  void UpdateRecentFilesMenu();
  void AddToRecentFiles(const QString& path);

  ThemeController* theme_controller_;
  ProjectDocument* document_;
  ValidationController* validation_controller_;
  ValidationIssuesModel* issues_model_;
  QMenu* recent_files_menu_ = nullptr;
  QLabel* placeholder_label_ = nullptr;
  QLabel* validation_status_label_ = nullptr;
};

}  // namespace videosynth::gui
