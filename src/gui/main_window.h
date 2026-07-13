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
#include <QStringList>

#include "generation_controller.h"
#include "log_message_model.h"
#include "preview_pane.h"
#include "project_document.h"
#include "project_settings_editor.h"
#include "section_editor.h"
#include "section_list_dock.h"
#include "source_probe_controller.h"
#include "theme_controller.h"
#include "validation_controller.h"
#include "validation_issues_model.h"

class QAction;
class QLabel;
class QMenu;
class QProgressBar;
class QTabWidget;

namespace videosynth::gui {

// Main window shell: menu bar (File / Edit / Project / Generate / View /
// Help), status bar, issues dock, sections dock, log dock, and the central
// tabs (project settings, per-section editors, and the signal preview) over
// a ProjectDocument with file lifecycle (New / Open / Save / Save As /
// Recent Files). Generation runs in-process on a worker thread through
// GenerationController with progress, logging, and cancellation.
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
  void OnPreferences();
  void OnGenerate();
  void OnGenerationFinished(GenerationController::RunStatus status);

 private:
  void BuildMenus();
  void BuildCentralEditors();
  void BuildSectionsDock();
  void BuildIssuesDock();
  void BuildLogDock();
  void BuildGenerationStatusWidgets();
  void ConnectGenerationController();
  void ShowGenerationSummary();
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
  SourceProbeController* probe_controller_;
  GenerationController* generation_controller_;
  LogMessageModel* log_model_;
  QMenu* recent_files_menu_ = nullptr;
  QLabel* validation_status_label_ = nullptr;
  QProgressBar* generation_progress_bar_ = nullptr;
  QAction* generate_action_ = nullptr;
  QAction* cancel_generation_action_ = nullptr;
  QTabWidget* editor_tabs_ = nullptr;
  ProjectSettingsEditor* project_settings_editor_ = nullptr;
  SectionEditor* section_editor_ = nullptr;
  PreviewPane* preview_pane_ = nullptr;
  SectionListDock* section_list_dock_ = nullptr;
  // Output artefacts of the most recently started run, for the completion
  // summary dialog.
  QStringList last_run_artefacts_;
};

}  // namespace videosynth::gui
