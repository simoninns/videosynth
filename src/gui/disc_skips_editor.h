/*
 * File:        disc_skips_editor.h
 * Module:      gui
 * Purpose:     Table editor for project-level disc skip events
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <QStyledItemDelegate>
#include <QWidget>

#include "disc_skips_model.h"
#include "project_document.h"

class QLabel;
class QPushButton;
class QTableView;

namespace videosynth::gui {

// Combo-box editor for the direction column ("forward" / "backward").
//
// Thread-safety: NOT thread-safe. GUI (main) thread only.
class DiscSkipDirectionDelegate : public QStyledItemDelegate {
  Q_OBJECT

 public:
  using QStyledItemDelegate::QStyledItemDelegate;

  QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                        const QModelIndex& index) const override;
  void setEditorData(QWidget* editor, const QModelIndex& index) const override;
  void setModelData(QWidget* editor, QAbstractItemModel* model,
                    const QModelIndex& index) const override;
};

// Editor for the optional project-level disc_skips table (at_frame,
// direction, count). Edits flow through DiscSkipsModel into
// ProjectDocument::SetDiscSkips.
//
// Thread-safety: NOT thread-safe. GUI (main) thread only.
class DiscSkipsEditor : public QWidget {
  Q_OBJECT

 public:
  // Does not take ownership of `document`; it must outlive the editor.
  explicit DiscSkipsEditor(ProjectDocument* document,
                           QWidget* parent = nullptr);

 private:
  void UpdateFrameRangeHint();

  ProjectDocument* document_;
  DiscSkipsModel* model_;
  QTableView* view_ = nullptr;
  QPushButton* remove_button_ = nullptr;
  QLabel* range_hint_ = nullptr;
};

}  // namespace videosynth::gui
