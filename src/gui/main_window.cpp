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
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPixmap>
#include <QSettings>
#include <QStatusBar>
#include <QStringList>
#include <QVBoxLayout>

#include "project_templates.h"
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

}  // namespace

MainWindow::MainWindow(ThemeController* theme_controller, QWidget* parent)
    : QMainWindow(parent),
      theme_controller_(theme_controller),
      document_(new ProjectDocument(this)),
      validation_controller_(new ValidationController({}, this)),
      issues_model_(new ValidationIssuesModel(this)) {
  setWindowIcon(QIcon(QStringLiteral(":/videosynth-gui/icon.png")));

  BuildMenus();
  BuildCentralPlaceholder();
  BuildIssuesDock();
  statusBar()->showMessage(tr("Ready"));
  validation_status_label_ = new QLabel(this);
  statusBar()->addPermanentWidget(validation_status_label_);

  connect(document_, &ProjectDocument::DocumentChanged, this, [this] {
    validation_controller_->RequestValidation(document_->project());
  });
  connect(document_, &ProjectDocument::DocumentReset, this, [this] {
    validation_controller_->RequestValidation(document_->project());
    if (placeholder_label_ != nullptr) {
      placeholder_label_->setText(document_->display_name());
    }
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
  connect(theme_controller_, &ThemeController::ThemeChanged, this,
          [this](bool is_dark) { issues_model_->SetDarkTheme(is_dark); });

  UpdateWindowTitle();
  RestoreWindowGeometry();
}

void MainWindow::closeEvent(QCloseEvent* event) {
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

  QMenu* project_menu = menuBar()->addMenu(tr("&Project"));
  project_menu->addAction(tr("&Validate"), this, [this] {
    validation_controller_->RequestValidation(document_->project());
  });

  QMenu* generate_menu = menuBar()->addMenu(tr("&Generate"));
  generate_menu->addAction(tr("&Generate"), this, [] {})->setEnabled(false);
  generate_menu->addAction(tr("&Cancel"), this, [] {})->setEnabled(false);

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

void MainWindow::BuildCentralPlaceholder() {
  auto* central = new QWidget(this);
  auto* layout = new QVBoxLayout(central);

  auto* logo_label = new QLabel(central);
  logo_label->setPixmap(
      QPixmap(QStringLiteral(":/videosynth-gui/icon.png"))
          .scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  logo_label->setAlignment(Qt::AlignCenter);

  placeholder_label_ = new QLabel(tr("No project loaded"), central);
  placeholder_label_->setAlignment(Qt::AlignCenter);

  layout->addStretch();
  layout->addWidget(logo_label);
  layout->addWidget(placeholder_label_);
  layout->addStretch();

  setCentralWidget(central);
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
  // Editors attach to this hook in a later phase; surface the target for
  // now.
  emit IssueNavigationRequested(section_index);
  if (section_index >= 0) {
    statusBar()->showMessage(
        tr("Issue relates to section %1").arg(section_index + 1), 3000);
  } else {
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
