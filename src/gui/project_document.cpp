/*
 * File:        project_document.cpp
 * Module:      gui
 * Purpose:     In-memory project document with change signals and dirty state
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "project_document.h"

#include <QFileInfo>
#include <utility>

namespace videosynth::gui {

namespace {

// Swaps a whole-value slice of the project (project info, presets, output,
// disc skips): Apply and Revert exchange the stored value with the live one.
template <typename Value>
class SwapValueCommand final : public IDocumentCommand {
 public:
  using Accessor = Value& (*)(Project&);

  SwapValueCommand(Accessor accessor, Value new_value,
                   DocumentChange::Kind kind, QString description)
      : accessor_(accessor),
        value_(std::move(new_value)),
        kind_(kind),
        description_(std::move(description)) {}

  DocumentChange Apply(Project* project) override { return Swap(project); }
  DocumentChange Revert(Project* project) override { return Swap(project); }
  QString Description() const override { return description_; }

 private:
  DocumentChange Swap(Project* project) {
    std::swap(accessor_(*project), value_);
    DocumentChange change;
    change.kind = kind_;
    return change;
  }

  Accessor accessor_;
  Value value_;
  DocumentChange::Kind kind_;
  QString description_;
};

class InsertSectionCommand final : public IDocumentCommand {
 public:
  InsertSectionCommand(int index, Section section)
      : index_(index), section_(std::move(section)) {}

  DocumentChange Apply(Project* project) override {
    project->sections.insert(project->sections.begin() + index_, section_);
    return {DocumentChange::Kind::kSectionAdded, index_, -1};
  }

  DocumentChange Revert(Project* project) override {
    project->sections.erase(project->sections.begin() + index_);
    return {DocumentChange::Kind::kSectionRemoved, index_, -1};
  }

  QString Description() const override { return QStringLiteral("Add section"); }

 private:
  int index_;
  Section section_;
};

class RemoveSectionCommand final : public IDocumentCommand {
 public:
  explicit RemoveSectionCommand(int index) : index_(index) {}

  DocumentChange Apply(Project* project) override {
    removed_ = project->sections[static_cast<std::size_t>(index_)];
    project->sections.erase(project->sections.begin() + index_);
    return {DocumentChange::Kind::kSectionRemoved, index_, -1};
  }

  DocumentChange Revert(Project* project) override {
    project->sections.insert(project->sections.begin() + index_, removed_);
    return {DocumentChange::Kind::kSectionAdded, index_, -1};
  }

  QString Description() const override {
    return QStringLiteral("Remove section");
  }

 private:
  int index_;
  Section removed_;
};

class MoveSectionCommand final : public IDocumentCommand {
 public:
  MoveSectionCommand(int from, int to) : from_(from), to_(to) {}

  DocumentChange Apply(Project* project) override {
    Move(project, from_, to_);
    return {DocumentChange::Kind::kSectionMoved, from_, to_};
  }

  DocumentChange Revert(Project* project) override {
    Move(project, to_, from_);
    return {DocumentChange::Kind::kSectionMoved, to_, from_};
  }

  QString Description() const override {
    return QStringLiteral("Move section");
  }

 private:
  static void Move(Project* project, int from, int to) {
    Section section =
        std::move(project->sections[static_cast<std::size_t>(from)]);
    project->sections.erase(project->sections.begin() + from);
    project->sections.insert(project->sections.begin() + to,
                             std::move(section));
  }

  int from_;
  int to_;
};

class SetSectionCommand final : public IDocumentCommand {
 public:
  SetSectionCommand(int index, Section section)
      : index_(index), section_(std::move(section)) {}

  DocumentChange Apply(Project* project) override { return Swap(project); }
  DocumentChange Revert(Project* project) override { return Swap(project); }

  QString Description() const override {
    return QStringLiteral("Edit section");
  }

 private:
  DocumentChange Swap(Project* project) {
    std::swap(project->sections[static_cast<std::size_t>(index_)], section_);
    return {DocumentChange::Kind::kSectionEdited, index_, -1};
  }

  int index_;
  Section section_;
};

}  // namespace

ProjectDocument::ProjectDocument(QObject* parent) : QObject(parent) {}

QString ProjectDocument::display_name() const {
  if (!file_path_.isEmpty()) {
    return QFileInfo(file_path_).fileName();
  }
  if (!project_.name.empty()) {
    return QString::fromStdString(project_.name);
  }
  return QStringLiteral("Untitled");
}

void ProjectDocument::ResetProject(Project project, const QString& file_path) {
  project_ = std::move(project);
  SetFilePath(file_path);
  SetModified(false);
  emit DocumentReset();
}

void ProjectDocument::MarkSaved(const QString& file_path) {
  SetFilePath(file_path);
  SetModified(false);
}

bool ProjectDocument::SetProjectInfo(const std::string& name,
                                     const std::string& version,
                                     const std::string& description) {
  if (project_.name == name && project_.version == version &&
      project_.description == description) {
    return false;
  }

  struct ProjectInfo {
    std::string name;
    std::string version;
    std::string description;
  };
  // SwapValueCommand needs a single addressable value; project info spans
  // three fields, so use a dedicated command.
  class SetProjectInfoCommand final : public IDocumentCommand {
   public:
    explicit SetProjectInfoCommand(ProjectInfo info) : info_(std::move(info)) {}

    DocumentChange Apply(Project* project) override { return Swap(project); }
    DocumentChange Revert(Project* project) override { return Swap(project); }
    QString Description() const override {
      return QStringLiteral("Edit project information");
    }

   private:
    DocumentChange Swap(Project* project) {
      std::swap(project->name, info_.name);
      std::swap(project->version, info_.version);
      std::swap(project->description, info_.description);
      return {DocumentChange::Kind::kProjectSettings, -1, -1};
    }

    ProjectInfo info_;
  };

  return ApplyCommand(std::make_unique<SetProjectInfoCommand>(
      ProjectInfo{name, version, description}));
}

bool ProjectDocument::SetCvbsPresets(const CvbsPresets& presets) {
  if (project_.cvbs_presets == presets) {
    return false;
  }
  return ApplyCommand(std::make_unique<SwapValueCommand<CvbsPresets>>(
      +[](Project& project) -> CvbsPresets& { return project.cvbs_presets; },
      presets, DocumentChange::Kind::kProjectSettings,
      QStringLiteral("Edit CVBS presets")));
}

bool ProjectDocument::SetOutputTargets(const OutputTargets& output) {
  if (project_.output == output) {
    return false;
  }
  return ApplyCommand(std::make_unique<SwapValueCommand<OutputTargets>>(
      +[](Project& project) -> OutputTargets& { return project.output; },
      output, DocumentChange::Kind::kProjectSettings,
      QStringLiteral("Edit output targets")));
}

bool ProjectDocument::SetDiscSkips(std::vector<DiscSkip> disc_skips) {
  if (project_.disc_skips == disc_skips) {
    return false;
  }
  return ApplyCommand(std::make_unique<SwapValueCommand<std::vector<DiscSkip>>>(
      +[](Project& project) -> std::vector<DiscSkip>& {
        return project.disc_skips;
      },
      std::move(disc_skips), DocumentChange::Kind::kDiscSkips,
      QStringLiteral("Edit disc skips")));
}

bool ProjectDocument::InsertSection(int index, Section section) {
  if (index == -1) {
    index = section_count();
  }
  if (index < 0 || index > section_count()) {
    return false;
  }
  return ApplyCommand(
      std::make_unique<InsertSectionCommand>(index, std::move(section)));
}

bool ProjectDocument::RemoveSection(int index) {
  if (!IsValidSectionIndex(index)) {
    return false;
  }
  return ApplyCommand(std::make_unique<RemoveSectionCommand>(index));
}

bool ProjectDocument::MoveSection(int from, int to) {
  if (!IsValidSectionIndex(from) || !IsValidSectionIndex(to) || from == to) {
    return false;
  }
  return ApplyCommand(std::make_unique<MoveSectionCommand>(from, to));
}

bool ProjectDocument::SetSection(int index, Section section) {
  if (!IsValidSectionIndex(index)) {
    return false;
  }
  if (project_.sections[static_cast<std::size_t>(index)] == section) {
    return false;
  }
  return ApplyCommand(
      std::make_unique<SetSectionCommand>(index, std::move(section)));
}

bool ProjectDocument::ApplyCommand(std::unique_ptr<IDocumentCommand> command) {
  if (command == nullptr) {
    return false;
  }

  const DocumentChange change = command->Apply(&project_);
  SetModified(true);
  AnnounceChange(change);
  // The command is discarded here; a future QUndoStack takes ownership at
  // this point to gain Revert support.
  return true;
}

void ProjectDocument::AnnounceChange(const DocumentChange& change) {
  switch (change.kind) {
    case DocumentChange::Kind::kProjectSettings:
      emit ProjectSettingsChanged();
      break;
    case DocumentChange::Kind::kSectionAdded:
      emit SectionAdded(change.index);
      break;
    case DocumentChange::Kind::kSectionRemoved:
      emit SectionRemoved(change.index);
      break;
    case DocumentChange::Kind::kSectionMoved:
      emit SectionMoved(change.index, change.to_index);
      break;
    case DocumentChange::Kind::kSectionEdited:
      emit SectionEdited(change.index);
      break;
    case DocumentChange::Kind::kDiscSkips:
      emit DiscSkipsChanged();
      break;
  }
  emit DocumentChanged();
}

void ProjectDocument::SetModified(bool modified) {
  if (modified_ == modified) {
    return;
  }
  modified_ = modified;
  emit ModifiedStateChanged(modified_);
}

bool ProjectDocument::IsValidSectionIndex(int index) const {
  return index >= 0 && index < section_count();
}

void ProjectDocument::SetFilePath(const QString& file_path) {
  if (file_path_ == file_path) {
    return;
  }
  file_path_ = file_path;
  emit FilePathChanged(file_path_);
}

}  // namespace videosynth::gui
