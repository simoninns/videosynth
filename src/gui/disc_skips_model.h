/*
 * File:        disc_skips_model.h
 * Module:      gui
 * Purpose:     Editable table model of project-level disc skip events
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <QAbstractTableModel>

#include "project_document.h"
#include "videosynth/model.h"

namespace videosynth::gui {

// Editable table over Project::disc_skips. Edits are routed through
// ProjectDocument::SetDiscSkips so they participate in dirty tracking and
// the command layer; external document changes reload the table.
//
// Thread-safety: NOT thread-safe. GUI (owning) thread only.
class DiscSkipsModel : public QAbstractTableModel {
  Q_OBJECT

 public:
  enum Column {
    kAtFrameColumn = 0,
    kDirectionColumn,
    kCountColumn,
    kColumnCount,
  };

  // Does not take ownership of `document`; it must outlive the model.
  explicit DiscSkipsModel(ProjectDocument* document, QObject* parent = nullptr);

  // Default arguments mirror the QAbstractItemModel base signatures.
  // NOLINTNEXTLINE(google-default-arguments)
  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  // NOLINTNEXTLINE(google-default-arguments)
  int columnCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role) const override;
  Qt::ItemFlags flags(const QModelIndex& index) const override;
  bool setData(const QModelIndex& index, const QVariant& value,
               int role) override;

  // Appends a forward skip of one frame at the first disc frame.
  void AddSkip();
  void RemoveSkip(int row);

 private:
  void Reload();
  bool Commit(std::vector<DiscSkip> skips);

  ProjectDocument* document_;
  std::vector<DiscSkip> skips_;
  // Guards against reloading in response to our own SetDiscSkips commit.
  bool committing_ = false;
};

}  // namespace videosynth::gui
