/*
 * File:        section_list_dock.cpp
 * Module:      gui
 * Purpose:     Sections dock content: ordered section list with add, remove,
 *              duplicate, and reorder operations
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "section_list_dock.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>

#include "project_templates.h"

namespace videosynth::gui {

SectionListDock::SectionListDock(ProjectDocument* document, QWidget* parent)
    : QWidget(parent),
      document_(document),
      model_(new SectionListModel(document, this)) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  auto* buttons = new QHBoxLayout();
  auto* add_button = new QToolButton(this);
  add_button->setText(tr("Add"));

  remove_button_ = new QToolButton(this);
  remove_button_->setText(tr("Remove"));
  duplicate_button_ = new QToolButton(this);
  duplicate_button_->setText(tr("Duplicate"));
  preview_button_ = new QToolButton(this);
  preview_button_->setText(tr("Preview"));
  preview_button_->setToolTip(
      tr("Show the section's first frame in the preview"));
  up_button_ = new QToolButton(this);
  up_button_->setText(tr("Up"));
  down_button_ = new QToolButton(this);
  down_button_->setText(tr("Down"));

  buttons->addWidget(add_button);
  buttons->addWidget(remove_button_);
  buttons->addWidget(duplicate_button_);
  buttons->addWidget(preview_button_);
  buttons->addStretch();
  buttons->addWidget(up_button_);
  buttons->addWidget(down_button_);
  layout->addLayout(buttons);

  view_ = new QTableView(this);
  view_->setModel(model_);
  view_->setSelectionBehavior(QAbstractItemView::SelectRows);
  view_->setSelectionMode(QAbstractItemView::SingleSelection);
  view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  view_->horizontalHeader()->setStretchLastSection(true);
  view_->horizontalHeader()->setSectionResizeMode(
      QHeaderView::ResizeToContents);
  view_->verticalHeader()->setVisible(false);
  layout->addWidget(view_);

  connect(add_button, &QToolButton::clicked, this,
          &SectionListDock::OnAddSection);
  connect(remove_button_, &QToolButton::clicked, this,
          &SectionListDock::OnRemove);
  connect(duplicate_button_, &QToolButton::clicked, this,
          &SectionListDock::OnDuplicate);
  connect(preview_button_, &QToolButton::clicked, this, [this] {
    const int index = current_section();
    if (index >= 0) {
      emit PreviewSectionRequested(index);
    }
  });
  connect(up_button_, &QToolButton::clicked, this, &SectionListDock::OnMoveUp);
  connect(down_button_, &QToolButton::clicked, this,
          &SectionListDock::OnMoveDown);

  connect(view_->selectionModel(), &QItemSelectionModel::currentRowChanged,
          this, [this](const QModelIndex& current, const QModelIndex&) {
            UpdateButtonStates();
            emit CurrentSectionChanged(current.isValid() ? current.row() : -1);
          });
  // Model resets clear the selection; keep button state consistent.
  connect(model_, &SectionListModel::modelReset, this, [this] {
    UpdateButtonStates();
    emit CurrentSectionChanged(current_section());
  });

  UpdateButtonStates();
}

int SectionListDock::current_section() const {
  const QModelIndex current = view_->currentIndex();
  return current.isValid() ? current.row() : -1;
}

void SectionListDock::SelectSection(int index) {
  if (index < 0 || index >= model_->rowCount()) {
    view_->clearSelection();
    return;
  }
  view_->setCurrentIndex(model_->index(index, 0));
}

void SectionListDock::OnAddSection() {
  AddSection(MakeProgressiveSectionTemplate(document_->section_count() + 1));
}

void SectionListDock::OnRemove() {
  const int index = current_section();
  if (index < 0) {
    return;
  }
  document_->RemoveSection(index);
  SelectSection(qMin(index, model_->rowCount() - 1));
}

void SectionListDock::OnDuplicate() {
  const int index = current_section();
  if (index < 0) {
    return;
  }
  const Project& project = document_->project();
  Section duplicate = MakeDuplicateSection(
      project.sections[static_cast<std::size_t>(index)], project.sections);
  document_->InsertSection(index + 1, std::move(duplicate));
  SelectSection(index + 1);
}

void SectionListDock::OnMoveUp() {
  const int index = current_section();
  if (index <= 0) {
    return;
  }
  document_->MoveSection(index, index - 1);
  SelectSection(index - 1);
}

void SectionListDock::OnMoveDown() {
  const int index = current_section();
  if (index < 0 || index >= model_->rowCount() - 1) {
    return;
  }
  document_->MoveSection(index, index + 1);
  SelectSection(index + 1);
}

void SectionListDock::AddSection(Section section) {
  document_->InsertSection(-1, std::move(section));
  SelectSection(model_->rowCount() - 1);
}

void SectionListDock::UpdateButtonStates() {
  const int index = current_section();
  const int count = model_->rowCount();
  remove_button_->setEnabled(index >= 0);
  duplicate_button_->setEnabled(index >= 0);
  preview_button_->setEnabled(index >= 0);
  up_button_->setEnabled(index > 0);
  down_button_->setEnabled(index >= 0 && index < count - 1);
}

}  // namespace videosynth::gui
