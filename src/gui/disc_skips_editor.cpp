/*
 * File:        disc_skips_editor.cpp
 * Module:      gui
 * Purpose:     Table editor for project-level disc skip events
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "disc_skips_editor.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableView>
#include <QVBoxLayout>

#include "line_injection_presenter.h"

namespace videosynth::gui {

QWidget* DiscSkipDirectionDelegate::createEditor(
    QWidget* parent, const QStyleOptionViewItem& /*option*/,
    const QModelIndex& /*index*/) const {
  auto* combo = new QComboBox(parent);
  combo->addItem(QStringLiteral("forward"));
  combo->addItem(QStringLiteral("backward"));
  return combo;
}

void DiscSkipDirectionDelegate::setEditorData(QWidget* editor,
                                              const QModelIndex& index) const {
  auto* combo = qobject_cast<QComboBox*>(editor);
  if (combo != nullptr) {
    combo->setCurrentText(index.data(Qt::EditRole).toString());
  }
}

void DiscSkipDirectionDelegate::setModelData(QWidget* editor,
                                             QAbstractItemModel* model,
                                             const QModelIndex& index) const {
  auto* combo = qobject_cast<QComboBox*>(editor);
  if (combo != nullptr) {
    model->setData(index, combo->currentText(), Qt::EditRole);
  }
}

DiscSkipsEditor::DiscSkipsEditor(ProjectDocument* document, QWidget* parent)
    : QWidget(parent),
      document_(document),
      model_(new DiscSkipsModel(document, this)) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  view_ = new QTableView(this);
  view_->setModel(model_);
  view_->setSelectionBehavior(QAbstractItemView::SelectRows);
  view_->setSelectionMode(QAbstractItemView::SingleSelection);
  view_->horizontalHeader()->setStretchLastSection(true);
  view_->verticalHeader()->setVisible(false);
  view_->setItemDelegateForColumn(DiscSkipsModel::kDirectionColumn,
                                  new DiscSkipDirectionDelegate(view_));
  layout->addWidget(view_);

  auto* buttons = new QHBoxLayout();
  auto* add_button = new QPushButton(tr("Add skip"), this);
  remove_button_ = new QPushButton(tr("Remove skip"), this);
  buttons->addWidget(add_button);
  buttons->addWidget(remove_button_);
  buttons->addStretch();
  layout->addLayout(buttons);

  range_hint_ = new QLabel(this);
  range_hint_->setWordWrap(true);
  layout->addWidget(range_hint_);

  connect(add_button, &QPushButton::clicked, this,
          [this] { model_->AddSkip(); });
  connect(remove_button_, &QPushButton::clicked, this, [this] {
    const QModelIndex current = view_->currentIndex();
    if (current.isValid()) {
      model_->RemoveSkip(current.row());
    }
  });
  connect(document_, &ProjectDocument::DocumentChanged, this,
          [this] { UpdateFrameRangeHint(); });
  connect(document_, &ProjectDocument::DocumentReset, this,
          [this] { UpdateFrameRangeHint(); });

  UpdateFrameRangeHint();
}

void DiscSkipsEditor::UpdateFrameRangeHint() {
  const int total = TotalDiscFrames(document_->project());
  range_hint_->setText(
      tr("Disc frames are 1-based; the current project has %1 disc frame(s).")
          .arg(total));
}

}  // namespace videosynth::gui
