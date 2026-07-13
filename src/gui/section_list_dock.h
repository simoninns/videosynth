/*
 * File:        section_list_dock.h
 * Module:      gui
 * Purpose:     Sections dock content: ordered section list with add, remove,
 *              duplicate, and reorder operations
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <QWidget>

#include "project_document.h"
#include "section_list_model.h"

class QTableView;
class QToolButton;

namespace videosynth::gui {

// Dock widget content for the ordered section list. Operations (add, remove,
// duplicate, move up/down) are routed through ProjectDocument commands;
// display sync comes from SectionListModel.
//
// Thread-safety: NOT thread-safe. GUI (main) thread only.
class SectionListDock : public QWidget {
  Q_OBJECT

 public:
  // Does not take ownership of `document`; it must outlive the dock.
  explicit SectionListDock(ProjectDocument* document,
                           QWidget* parent = nullptr);

  // Currently selected section index, or -1.
  int current_section() const;

  // Programmatic selection (issue navigation); emits CurrentSectionChanged.
  void SelectSection(int index);

 signals:
  void CurrentSectionChanged(int index);

  // "Preview this section": the preview should jump to the section's first
  // output frame.
  void PreviewSectionRequested(int index);

 private:
  void OnAddSection();
  void OnRemove();
  void OnDuplicate();
  void OnMoveUp();
  void OnMoveDown();

  void AddSection(Section section);
  void UpdateButtonStates();

  ProjectDocument* document_;
  SectionListModel* model_;
  QTableView* view_ = nullptr;
  QToolButton* remove_button_ = nullptr;
  QToolButton* duplicate_button_ = nullptr;
  QToolButton* preview_button_ = nullptr;
  QToolButton* up_button_ = nullptr;
  QToolButton* down_button_ = nullptr;
};

}  // namespace videosynth::gui
