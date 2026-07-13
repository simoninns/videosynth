/*
 * File:        main_window.cpp
 * Module:      gui
 * Purpose:     Application main window shell with menus and About dialog
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "main_window.h"

#include <QAction>
#include <QActionGroup>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QKeySequence>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QStatusBar>
#include <QStringList>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "disc_skips_editor.h"
#include "generation_preferences.h"
#include "preferences_dialog.h"
#include "project_templates.h"
#include "videosynth/audio_wav_writer.h"
#include "videosynth/dropout_injection_stage.h"
#include "videosynth/yaml_project_emitter.h"
#include "videosynth/yaml_project_parser.h"
#include "videosynth_version.h"

namespace videosynth::gui {

namespace {

constexpr const char* kGeometrySettingsKey = "main_window/geometry";
constexpr const char* kRecentFilesSettingsKey = "main_window/recent_files";
constexpr int kMaxRecentFiles = 10;

QString ProjectFileFilter() {
  return MainWindow::tr("VideoSynth projects (*.yaml *.yml);;All files (*)");
}

// True when at least one section enables the given optional capability, so
// the completion summary only lists artefacts the run actually produced.
bool AnySectionEnablesAudio(const Project& project) {
  for (const Section& section : project.sections) {
    if (section.audio.enabled) {
      return true;
    }
  }
  return false;
}

bool AnySectionEnablesDropouts(const Project& project) {
  for (const Section& section : project.sections) {
    if (section.dropouts.random.enabled || section.dropouts.scratch.enabled) {
      return true;
    }
  }
  return false;
}

}  // namespace

MainWindow::MainWindow(ThemeController* theme_controller, QWidget* parent)
    : QMainWindow(parent),
      theme_controller_(theme_controller),
      document_(new ProjectDocument(this)),
      validation_controller_(new ValidationController({}, this)),
      issues_model_(new ValidationIssuesModel(this)),
      probe_controller_(new SourceProbeController({}, this)),
      generation_controller_(new GenerationController({}, this)),
      log_model_(
          new LogMessageModel(LogMessageModel::kDefaultMaxEntries, this)) {
  setWindowIcon(QIcon(QStringLiteral(":/videosynth-gui/icon.png")));

  BuildMenus();
  BuildCentralEditors();
  BuildSectionsDock();
  BuildIssuesDock();
  BuildLogDock();
  statusBar()->showMessage(tr("Ready"));
  BuildGenerationStatusWidgets();
  validation_status_label_ = new QLabel(this);
  statusBar()->addPermanentWidget(validation_status_label_);
  ConnectGenerationController();

  connect(document_, &ProjectDocument::DocumentChanged, this, [this] {
    validation_controller_->RequestValidation(document_->project());
  });
  connect(document_, &ProjectDocument::DocumentReset, this, [this] {
    validation_controller_->RequestValidation(document_->project());
    section_editor_->SetCurrentSection(section_list_dock_->current_section());
    UpdateWindowTitle();
  });
  connect(document_, &ProjectDocument::ModifiedStateChanged, this,
          [this](bool modified) { setWindowModified(modified); });
  connect(document_, &ProjectDocument::FilePathChanged, this,
          [this](const QString&) { UpdateWindowTitle(); });
  connect(validation_controller_, &ValidationController::IssuesChanged, this,
          [this] {
            issues_model_->SetIssues(validation_controller_->issues());
            UpdateValidationStatus();
          });
  issues_model_->SetDarkTheme(theme_controller_->is_dark());
  log_model_->SetDarkTheme(theme_controller_->is_dark());
  connect(theme_controller_, &ThemeController::ThemeChanged, this,
          [this](bool is_dark) {
            issues_model_->SetDarkTheme(is_dark);
            log_model_->SetDarkTheme(is_dark);
          });

  // Start from the template so the editors show a valid project instead of
  // an empty document (matches File > New; not marked modified).
  document_->ResetProject(MakeDefaultPalProject(), QString());
  section_list_dock_->SelectSection(0);

  UpdateWindowTitle();
  RestoreWindowGeometry();
}

void MainWindow::closeEvent(QCloseEvent* event) {
  if (generation_controller_->is_running()) {
    const QMessageBox::StandardButton choice = QMessageBox::question(
        this, tr("Generation In Progress"),
        tr("A generation run is in progress.\nCancel it and quit?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (choice != QMessageBox::Yes) {
      event->ignore();
      return;
    }
    // The controller destructor joins the worker; the pipeline stops at its
    // next cancellation check and removes in-progress output files.
    generation_controller_->RequestCancellation();
  }

  if (!MaybeSave()) {
    event->ignore();
    return;
  }

  QSettings settings;
  settings.setValue(QLatin1String(kGeometrySettingsKey), saveGeometry());
  QMainWindow::closeEvent(event);
}

void MainWindow::BuildMenus() {
  QMenu* file_menu = menuBar()->addMenu(tr("&File"));
  file_menu->addAction(tr("&New Project"), QKeySequence::New, this,
                       &MainWindow::OnNewProject);
  file_menu->addAction(tr("&Open Project…"), QKeySequence::Open, this,
                       &MainWindow::OnOpenProject);
  recent_files_menu_ = file_menu->addMenu(tr("Open &Recent"));
  UpdateRecentFilesMenu();
  file_menu->addSeparator();
  file_menu->addAction(tr("&Save"), QKeySequence::Save, this,
                       &MainWindow::OnSave);
  file_menu->addAction(tr("Save &As…"), QKeySequence::SaveAs, this,
                       &MainWindow::OnSaveAs);
  file_menu->addSeparator();
  file_menu->addAction(tr("E&xit"), QKeySequence::Quit, this,
                       &MainWindow::close);

  // Edit: undo/redo lands with the undo-stack integration.
  QMenu* edit_menu = menuBar()->addMenu(tr("&Edit"));
  edit_menu->addAction(tr("&Undo"), QKeySequence::Undo, this, [] {})
      ->setEnabled(false);
  edit_menu->addAction(tr("&Redo"), QKeySequence::Redo, this, [] {})
      ->setEnabled(false);
  edit_menu->addSeparator();
  edit_menu->addAction(tr("&Preferences…"), QKeySequence::Preferences, this,
                       &MainWindow::OnPreferences);

  QMenu* project_menu = menuBar()->addMenu(tr("&Project"));
  project_menu->addAction(tr("&Validate"), this, [this] {
    validation_controller_->RequestValidation(document_->project());
  });

  QMenu* generate_menu = menuBar()->addMenu(tr("&Generate"));
  generate_menu->addAction(tr("&Validate"), this, [this] {
    validation_controller_->RequestValidation(document_->project());
  });
  generate_action_ = generate_menu->addAction(
      tr("&Generate"), QKeySequence(Qt::CTRL | Qt::Key_G), this,
      &MainWindow::OnGenerate);
  cancel_generation_action_ =
      generate_menu->addAction(tr("&Cancel Generation"), this, [this] {
        generation_controller_->RequestCancellation();
        statusBar()->showMessage(tr("Cancelling generation…"));
      });
  cancel_generation_action_->setEnabled(false);

  QMenu* view_menu = menuBar()->addMenu(tr("&View"));
  QMenu* theme_menu = view_menu->addMenu(tr("&Theme"));
  auto* theme_group = new QActionGroup(theme_menu);
  theme_group->setExclusive(true);

  const struct {
    ThemeManager::Mode mode;
    QString label;
  } theme_entries[] = {
      {ThemeManager::Mode::kAuto, tr("&Auto")},
      {ThemeManager::Mode::kLight, tr("&Light")},
      {ThemeManager::Mode::kDark, tr("&Dark")},
  };

  for (const auto& entry : theme_entries) {
    QAction* action = theme_menu->addAction(entry.label);
    action->setCheckable(true);
    action->setChecked(theme_controller_->mode() == entry.mode);
    theme_group->addAction(action);
    const ThemeManager::Mode mode = entry.mode;
    connect(action, &QAction::triggered, this,
            [this, mode] { theme_controller_->SetMode(mode); });
  }

  QMenu* help_menu = menuBar()->addMenu(tr("&Help"));
  help_menu->addAction(tr("&About"), this, &MainWindow::OnAbout);
}

void MainWindow::BuildCentralEditors() {
  editor_tabs_ = new QTabWidget(this);

  // Project tab: settings form plus the project-level disc skips table.
  auto* project_page = new QWidget(editor_tabs_);
  auto* project_layout = new QVBoxLayout(project_page);
  project_layout->setContentsMargins(0, 0, 0, 0);
  auto* project_scroll = new QScrollArea(project_page);
  project_scroll->setWidgetResizable(true);
  project_scroll->setFrameShape(QFrame::NoFrame);
  auto* project_content = new QWidget(project_scroll);
  auto* project_content_layout = new QVBoxLayout(project_content);
  project_settings_editor_ =
      new ProjectSettingsEditor(document_, project_content);
  project_content_layout->addWidget(project_settings_editor_);
  auto* disc_skips_group = new QGroupBox(tr("Disc Skips"), project_content);
  auto* disc_skips_layout = new QVBoxLayout(disc_skips_group);
  disc_skips_layout->addWidget(
      new DiscSkipsEditor(document_, disc_skips_group));
  project_content_layout->addWidget(disc_skips_group);
  project_content_layout->addStretch();
  project_scroll->setWidget(project_content);
  project_layout->addWidget(project_scroll);
  editor_tabs_->addTab(project_page, tr("Project"));

  section_editor_ = new SectionEditor(document_, probe_controller_, this);
  editor_tabs_->addTab(section_editor_, tr("Section"));

  setCentralWidget(editor_tabs_);
}

void MainWindow::BuildSectionsDock() {
  auto* dock = new QDockWidget(tr("Sections"), this);
  dock->setObjectName(QStringLiteral("sections_dock"));
  section_list_dock_ = new SectionListDock(document_, dock);
  dock->setWidget(section_list_dock_);
  addDockWidget(Qt::LeftDockWidgetArea, dock);

  connect(section_list_dock_, &SectionListDock::CurrentSectionChanged, this,
          [this](int index) {
            section_editor_->SetCurrentSection(index);
            if (index >= 0) {
              editor_tabs_->setCurrentWidget(section_editor_);
            }
          });
}

void MainWindow::BuildIssuesDock() {
  auto* dock = new QDockWidget(tr("Issues"), this);
  dock->setObjectName(QStringLiteral("issues_dock"));

  auto* view = new QListView(dock);
  view->setModel(issues_model_);
  view->setEditTriggers(QAbstractItemView::NoEditTriggers);
  view->setSelectionMode(QAbstractItemView::SingleSelection);
  view->setWordWrap(true);
  connect(view, &QListView::doubleClicked, this, &MainWindow::OnIssueActivated);

  dock->setWidget(view);
  addDockWidget(Qt::BottomDockWidgetArea, dock);
}

void MainWindow::BuildLogDock() {
  auto* dock = new QDockWidget(tr("Log"), this);
  dock->setObjectName(QStringLiteral("log_dock"));

  auto* view = new QListView(dock);
  view->setModel(log_model_);
  view->setEditTriggers(QAbstractItemView::NoEditTriggers);
  view->setSelectionMode(QAbstractItemView::ExtendedSelection);
  view->setUniformItemSizes(true);

  // Follow the newest message while the run is producing output.
  connect(log_model_, &QAbstractItemModel::rowsInserted, view,
          [view] { view->scrollToBottom(); });

  dock->setWidget(view);
  addDockWidget(Qt::BottomDockWidgetArea, dock);
}

void MainWindow::BuildGenerationStatusWidgets() {
  generation_progress_bar_ = new QProgressBar(this);
  generation_progress_bar_->setVisible(false);
  generation_progress_bar_->setMaximumWidth(300);
  generation_progress_bar_->setFormat(tr("%v / %m frames"));
  statusBar()->addPermanentWidget(generation_progress_bar_);
}

void MainWindow::ConnectGenerationController() {
  connect(generation_controller_, &GenerationController::RunStarted, this,
          [this] {
            generate_action_->setEnabled(false);
            cancel_generation_action_->setEnabled(true);
            // Busy indicator until the first frame progress arrives.
            generation_progress_bar_->setRange(0, 0);
            generation_progress_bar_->setVisible(true);
            statusBar()->showMessage(tr("Generating…"));
          });
  connect(
      generation_controller_, &GenerationController::StageStarted, this,
      [this](const QString& stage_name) {
        statusBar()->showMessage(tr("Generation stage: %1").arg(stage_name));
      });
  connect(
      generation_controller_, &GenerationController::FrameProgress, this,
      [this](qulonglong frames_completed, qulonglong frames_total) {
        generation_progress_bar_->setRange(0, static_cast<int>(frames_total));
        generation_progress_bar_->setValue(static_cast<int>(frames_completed));
      });
  connect(generation_controller_, &GenerationController::LogMessageReceived,
          this, [this](int severity, const QString& message) {
            log_model_->Append(static_cast<LogSeverity>(severity), message);
          });
  connect(generation_controller_, &GenerationController::RunFinished, this,
          &MainWindow::OnGenerationFinished);
}

void MainWindow::OnPreferences() {
  QSettings settings;
  PreferencesDialog dialog(LoadGenerationPreferences(settings), this);
  if (dialog.exec() == QDialog::Accepted) {
    const GenerationPreferences preferences = dialog.preferences();
    SaveGenerationPreferences(preferences, &settings);
    statusBar()->showMessage(tr("Preferences saved"), 3000);
  }
}

void MainWindow::OnGenerate() {
  if (generation_controller_->is_running()) {
    return;
  }

  if (document_->is_modified()) {
    QMessageBox prompt(this);
    prompt.setWindowTitle(tr("Unsaved Changes"));
    prompt.setText(tr("The project \"%1\" has unsaved changes.")
                       .arg(document_->display_name()));
    prompt.setInformativeText(
        tr("Save before generating, or generate from the current in-memory "
           "project?"));
    QPushButton* save_button =
        prompt.addButton(tr("Save and Generate"), QMessageBox::AcceptRole);
    prompt.addButton(tr("Generate Without Saving"), QMessageBox::ActionRole);
    QPushButton* cancel_button = prompt.addButton(QMessageBox::Cancel);
    prompt.setDefaultButton(save_button);
    prompt.exec();
    if (prompt.clickedButton() == cancel_button) {
      return;
    }
    if (prompt.clickedButton() == save_button && !OnSave()) {
      return;
    }
  }

  // Relative paths in the project are anchored to the project file's
  // directory (or the working directory for never-saved projects) because
  // the run executes on a worker thread.
  const QString base_dir =
      document_->file_path().isEmpty()
          ? QDir::currentPath()
          : QFileInfo(document_->file_path()).absolutePath();
  const Project project =
      ResolveProjectPaths(document_->project(), base_dir.toStdString());

  last_run_artefacts_.clear();
  last_run_artefacts_.append(QString::fromStdString(project.output.video_path));
  last_run_artefacts_.append(
      QString::fromStdString(project.output.metadata_path));
  if (AnySectionEnablesAudio(project)) {
    last_run_artefacts_.append(QString::fromStdString(
        AudioWavWriter::DeriveAudioPath(project.output.video_path)));
  }
  if (AnySectionEnablesDropouts(project)) {
    last_run_artefacts_.append(
        QString::fromStdString(DropoutInjectionStage::DeriveSidecarPath(
            project.output.metadata_path)));
  }

  const QSettings settings;
  const RunOptions options =
      MakeRunOptions(LoadGenerationPreferences(settings));
  log_model_->Clear();
  generation_controller_->StartGeneration(project, options);
}

void MainWindow::OnGenerationFinished(GenerationController::RunStatus status) {
  generate_action_->setEnabled(true);
  cancel_generation_action_->setEnabled(false);
  generation_progress_bar_->setVisible(false);

  switch (status) {
    case GenerationController::RunStatus::kSucceeded:
      statusBar()->showMessage(tr("Generation completed"), 5000);
      ShowGenerationSummary();
      break;
    case GenerationController::RunStatus::kCancelled:
      statusBar()->showMessage(tr("Generation cancelled"), 5000);
      break;
    case GenerationController::RunStatus::kFailed:
      statusBar()->showMessage(tr("Generation failed"), 5000);
      QMessageBox::critical(
          this, tr("Generation Failed"),
          tr("The generation run failed. See the Log panel for details."));
      break;
  }
}

void MainWindow::ShowGenerationSummary() {
  QMessageBox summary(this);
  summary.setWindowTitle(tr("Generation Complete"));
  summary.setIcon(QMessageBox::Information);

  summary.setText(tr("Generation completed successfully."));
  summary.setInformativeText(
      tr("Output files:\n%1").arg(last_run_artefacts_.join(QLatin1Char('\n'))));

  QPushButton* open_folder_button =
      summary.addButton(tr("Open Containing Folder"), QMessageBox::ActionRole);
  summary.addButton(QMessageBox::Ok);
  summary.setDefaultButton(QMessageBox::Ok);
  summary.exec();

  if (summary.clickedButton() == open_folder_button &&
      !last_run_artefacts_.isEmpty()) {
    const QString folder =
        QFileInfo(last_run_artefacts_.first()).absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
  }
}

void MainWindow::RestoreWindowGeometry() {
  const QSettings settings;
  const QByteArray geometry =
      settings.value(QLatin1String(kGeometrySettingsKey)).toByteArray();
  if (!geometry.isEmpty()) {
    restoreGeometry(geometry);
  } else {
    resize(1024, 768);
  }
}

void MainWindow::OnNewProject() {
  if (!MaybeSave()) {
    return;
  }
  document_->ResetProject(MakeDefaultPalProject(), QString());
  section_list_dock_->SelectSection(0);
  statusBar()->showMessage(tr("New project created"), 3000);
}

void MainWindow::OnOpenProject() {
  if (!MaybeSave()) {
    return;
  }
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Open Project"), QString(), ProjectFileFilter());
  if (path.isEmpty()) {
    return;
  }
  LoadProjectFromFile(path);
}

bool MainWindow::OnSave() {
  if (document_->file_path().isEmpty()) {
    return OnSaveAs();
  }
  return SaveToPath(document_->file_path());
}

bool MainWindow::OnSaveAs() {
  const QString path = QFileDialog::getSaveFileName(
      this, tr("Save Project As"), document_->file_path(), ProjectFileFilter());
  if (path.isEmpty()) {
    return false;
  }
  return SaveToPath(path);
}

void MainWindow::OnIssueActivated(const QModelIndex& index) {
  if (!index.isValid()) {
    return;
  }
  const int section_index =
      index.data(ValidationIssuesModel::kSectionIndexRole).toInt();
  emit IssueNavigationRequested(section_index);

  // Navigate to the offending editor: section issues focus that section's
  // editor; project-level issues focus the project settings tab.
  if (section_index >= 0 && section_index < document_->section_count()) {
    section_list_dock_->SelectSection(section_index);
    section_editor_->SetCurrentSection(section_index);
    editor_tabs_->setCurrentWidget(section_editor_);
    statusBar()->showMessage(
        tr("Issue relates to section %1").arg(section_index + 1), 3000);
  } else {
    editor_tabs_->setCurrentIndex(0);
    statusBar()->showMessage(tr("Project-level issue"), 3000);
  }
}

bool MainWindow::MaybeSave() {
  if (!document_->is_modified()) {
    return true;
  }

  const QMessageBox::StandardButton choice = QMessageBox::warning(
      this, tr("Unsaved Changes"),
      tr("The project \"%1\" has unsaved changes.\nDo you want to save them?")
          .arg(document_->display_name()),
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
      QMessageBox::Save);

  switch (choice) {
    case QMessageBox::Save:
      return OnSave();
    case QMessageBox::Discard:
      return true;
    default:
      return false;
  }
}

bool MainWindow::SaveToPath(const QString& path) {
  YamlProjectEmitter emitter;
  std::string error;
  if (!emitter.EmitFile(document_->project(), path.toStdString(), &error)) {
    QMessageBox::critical(this, tr("Save Failed"),
                          QString::fromStdString(error));
    return false;
  }

  document_->MarkSaved(path);
  AddToRecentFiles(path);
  UpdateWindowTitle();
  statusBar()->showMessage(tr("Saved %1").arg(path), 3000);
  return true;
}

void MainWindow::LoadProjectFromFile(const QString& path) {
  YamlProjectParser parser;
  ParseResult result = parser.ParseFile(path.toStdString());
  if (!result.ok) {
    QStringList details;
    for (const std::string& error : result.errors) {
      details.append(QString::fromStdString(error));
    }
    QMessageBox::critical(
        this, tr("Open Failed"),
        tr("Could not open \"%1\":\n%2").arg(path, details.join('\n')));
    return;
  }

  document_->ResetProject(std::move(result.project), path);
  section_list_dock_->SelectSection(0);
  AddToRecentFiles(path);
  statusBar()->showMessage(tr("Opened %1").arg(path), 3000);
}

void MainWindow::UpdateWindowTitle() {
  setWindowTitle(
      QStringLiteral("%1[*] — videosynth").arg(document_->display_name()));
  setWindowModified(document_->is_modified());
}

void MainWindow::UpdateValidationStatus() {
  const int errors = issues_model_->error_count();
  const int warnings = issues_model_->warning_count();
  if (errors == 0 && warnings == 0) {
    validation_status_label_->setText(tr("Project valid"));
  } else {
    validation_status_label_->setText(
        tr("%1 error(s), %2 warning(s)").arg(errors).arg(warnings));
  }
}

void MainWindow::UpdateRecentFilesMenu() {
  recent_files_menu_->clear();

  const QSettings settings;
  const QStringList recent =
      settings.value(QLatin1String(kRecentFilesSettingsKey)).toStringList();

  for (const QString& path : recent) {
    QAction* action = recent_files_menu_->addAction(QFileInfo(path).fileName());
    action->setToolTip(path);
    connect(action, &QAction::triggered, this, [this, path] {
      if (!MaybeSave()) {
        return;
      }
      LoadProjectFromFile(path);
    });
  }
  recent_files_menu_->setEnabled(!recent.isEmpty());
}

void MainWindow::AddToRecentFiles(const QString& path) {
  QSettings settings;
  QStringList recent =
      settings.value(QLatin1String(kRecentFilesSettingsKey)).toStringList();
  recent.removeAll(path);
  recent.prepend(path);
  while (recent.size() > kMaxRecentFiles) {
    recent.removeLast();
  }
  settings.setValue(QLatin1String(kRecentFilesSettingsKey), recent);
  UpdateRecentFilesMenu();
}

void MainWindow::OnAbout() {
  QMessageBox about_box(this);
  about_box.setWindowTitle(tr("About VideoSynth"));
  about_box.setIconPixmap(
      QPixmap(QStringLiteral(":/videosynth-gui/icon.png"))
          .scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation));

  const QString about_text =
      QStringLiteral(
          "<h2>VideoSynth</h2>"
          "<p><b>Version:</b> %1</p>"
          "<p>Analogue video signal generator</p>"
          "<p><b>Copyright:</b> © 2026 Simon Inns</p>"
          "<p><b>License:</b> GNU General Public License v3.0 or later</p>"
          "<p>This program is free software: you can redistribute it and/or "
          "modify it under the terms of the GNU General Public License as "
          "published by the Free Software Foundation, either version 3 of "
          "the License, or (at your option) any later version.</p>"
          "<p>This program is distributed in the hope that it will be "
          "useful, but WITHOUT ANY WARRANTY; without even the implied "
          "warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. "
          "See the GNU General Public License for more details.</p>"
          "<p>You should have received a copy of the GNU General Public "
          "License along with this program. If not, see "
          "<a href='https://www.gnu.org/licenses/'>"
          "https://www.gnu.org/licenses/</a>.</p>")
          .arg(QStringLiteral(VIDEOSYNTH_VERSION));

  about_box.setText(about_text);
  about_box.setTextFormat(Qt::RichText);
  about_box.setTextInteractionFlags(Qt::TextBrowserInteraction);
  about_box.exec();
}

}  // namespace videosynth::gui
