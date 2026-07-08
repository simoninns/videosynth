/*
 * File:        project_document.h
 * Module:      gui
 * Purpose:     In-memory project document with change signals and dirty state
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <QObject>
#include <QString>
#include <memory>
#include <string>
#include <vector>

#include "videosynth/model.h"

namespace videosynth::gui {

// Describes the change a document command produced, so ProjectDocument can
// emit the matching granular signal after applying (or later reverting) it.
struct DocumentChange {
  enum class Kind {
    kProjectSettings,  // project info, cvbs presets, or output targets
    kSectionAdded,
    kSectionRemoved,
    kSectionMoved,
    kSectionEdited,
    kDiscSkips,
  };

  Kind kind = Kind::kProjectSettings;
  // Section index the change applies to (kSectionAdded/Removed/Edited and
  // the origin of kSectionMoved); -1 when not applicable.
  int index = -1;
  // Destination index of kSectionMoved; -1 otherwise.
  int to_index = -1;
};

// Command-pattern mutation of a videosynth::Project. Every document edit is
// routed through a command so a QUndoStack can be layered on later without
// changing the mutation API: Apply performs the edit, Revert restores the
// prior state, and the returned DocumentChange drives signal emission.
//
// Thread-safety: NOT thread-safe. Commands run on the document's thread.
class IDocumentCommand {
 public:
  virtual ~IDocumentCommand() = default;

  // Performs the mutation and returns the change to announce.
  virtual DocumentChange Apply(Project* project) = 0;

  // Restores the state prior to Apply and returns the change to announce.
  virtual DocumentChange Revert(Project* project) = 0;

  // Short human-readable description for a future undo-stack UI.
  virtual QString Description() const = 0;
};

// Owns the in-memory videosynth::Project the GUI edits, together with its
// file path and dirty flag. All mutations go through command objects (see
// IDocumentCommand) and emit granular change signals; DocumentChanged fires
// after every granular signal so listeners (validation, previews) can attach
// once.
//
// Thread-safety: NOT thread-safe. Must be used from the thread that owns it
// (normally the GUI thread). Workers must copy project() and marshal results
// back via queued connections.
class ProjectDocument : public QObject {
  Q_OBJECT

 public:
  explicit ProjectDocument(QObject* parent = nullptr);

  const Project& project() const { return project_; }
  QString file_path() const { return file_path_; }
  bool is_modified() const { return modified_; }
  int section_count() const {
    return static_cast<int>(project_.sections.size());
  }

  // Display name for window titles: file base name, else the project name,
  // else "Untitled".
  QString display_name() const;

  // Replaces the whole document (File > New / Open). Clears the dirty flag
  // and emits DocumentReset. `file_path` is empty for unsaved templates.
  void ResetProject(Project project, const QString& file_path);

  // Records a successful save to `file_path`: clears the dirty flag and
  // updates the path (Save As may change it).
  void MarkSaved(const QString& file_path);

  // --- Command-routed mutations -------------------------------------------
  // Each returns false (and leaves the document untouched) when the request
  // is a no-op or the index is out of range.

  bool SetProjectInfo(const std::string& name, const std::string& version,
                      const std::string& description);
  bool SetCvbsPresets(const CvbsPresets& presets);
  bool SetOutputTargets(const OutputTargets& output);
  bool SetDiscSkips(std::vector<DiscSkip> disc_skips);

  // Inserts at `index`, or appends when index == -1 or index == count.
  bool InsertSection(int index, Section section);
  bool RemoveSection(int index);
  bool MoveSection(int from, int to);
  bool SetSection(int index, Section section);

  // Generic entry point used by the typed mutations above; exposed so future
  // editors (and an undo stack) can submit custom commands.
  bool ApplyCommand(std::unique_ptr<IDocumentCommand> command);

 signals:
  // Granular change signals; exactly one fires per applied command.
  void ProjectSettingsChanged();
  void SectionAdded(int index);
  void SectionRemoved(int index);
  void SectionMoved(int from, int to);
  void SectionEdited(int index);
  void DiscSkipsChanged();

  // Fires after every granular signal above (any document mutation).
  void DocumentChanged();

  // Whole-document replacement (New/Open).
  void DocumentReset();

  void ModifiedStateChanged(bool modified);
  void FilePathChanged(const QString& file_path);

 private:
  void AnnounceChange(const DocumentChange& change);
  void SetModified(bool modified);
  void SetFilePath(const QString& file_path);
  bool IsValidSectionIndex(int index) const;

  Project project_;
  QString file_path_;
  bool modified_ = false;
};

}  // namespace videosynth::gui
