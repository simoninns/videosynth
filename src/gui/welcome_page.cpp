/*
 * File:        welcome_page.cpp
 * Module:      gui
 * Purpose:     Empty-state landing surface shown when no project is open
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "welcome_page.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace videosynth::gui {

WelcomePage::WelcomePage(QWidget* parent) : QWidget(parent) {
  auto* outer = new QHBoxLayout(this);
  outer->addStretch();

  auto* column = new QVBoxLayout();
  column->addStretch();

  auto* logo = new QLabel(this);
  logo->setPixmap(
      QIcon(QStringLiteral(":/videosynth-gui/icon.png")).pixmap(128, 128));
  logo->setAlignment(Qt::AlignCenter);
  column->addWidget(logo);

  auto* title = new QLabel(tr("<h2>VideoSynth</h2>"), this);
  title->setAlignment(Qt::AlignCenter);
  column->addWidget(title);

  auto* subtitle =
      new QLabel(tr("Create a new project or open an existing one."), this);
  subtitle->setAlignment(Qt::AlignCenter);
  column->addWidget(subtitle);
  column->addSpacing(12);

  auto* new_button = new QPushButton(tr("New Project…"), this);
  auto* open_button = new QPushButton(tr("Open Project…"), this);
  new_button->setDefault(true);
  column->addWidget(new_button);
  column->addWidget(open_button);

  auto* recent_label = new QLabel(tr("Recent projects"), this);
  column->addSpacing(12);
  column->addWidget(recent_label);
  recent_list_ = new QListWidget(this);
  recent_list_->setMinimumWidth(360);
  recent_list_->setMaximumHeight(160);
  column->addWidget(recent_list_);

  column->addStretch();
  outer->addLayout(column);
  outer->addStretch();

  connect(new_button, &QPushButton::clicked, this,
          &WelcomePage::NewProjectRequested);
  connect(open_button, &QPushButton::clicked, this,
          &WelcomePage::OpenProjectRequested);
  connect(recent_list_, &QListWidget::itemActivated, this,
          [this](QListWidgetItem* item) {
            if (item != nullptr) {
              emit RecentFileRequested(item->data(Qt::UserRole).toString());
            }
          });
}

void WelcomePage::SetRecentFiles(const QStringList& paths) {
  recent_list_->clear();
  for (const QString& path : paths) {
    auto* item = new QListWidgetItem(QFileInfo(path).fileName(), recent_list_);
    item->setToolTip(path);
    item->setData(Qt::UserRole, path);
  }
  recent_list_->setVisible(!paths.isEmpty());
}

}  // namespace videosynth::gui
