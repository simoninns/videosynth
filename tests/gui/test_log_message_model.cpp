/*
 * File:        test_log_message_model.cpp
 * Module:      gui_tests
 * Purpose:     Unit tests for the bounded log dock list model
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <QBrush>
#include <QVariant>

#include "log_message_model.h"

namespace videosynth::gui {
namespace {

TEST(LogMessageModelTest, AppendExposesMessageAndSeverityRoles) {
  LogMessageModel model;
  model.Append(LogSeverity::kWarning, QStringLiteral("careful"));

  ASSERT_EQ(model.rowCount(), 1);
  const QModelIndex index = model.index(0);
  EXPECT_EQ(index.data(Qt::DisplayRole).toString(), QStringLiteral("careful"));
  EXPECT_EQ(index.data(LogMessageModel::kSeverityRole).toInt(),
            static_cast<int>(LogSeverity::kWarning));
}

TEST(LogMessageModelTest, BoundedCapacityEvictsOldestEntry) {
  LogMessageModel model(3);
  model.Append(LogSeverity::kInfo, QStringLiteral("one"));
  model.Append(LogSeverity::kInfo, QStringLiteral("two"));
  model.Append(LogSeverity::kInfo, QStringLiteral("three"));
  model.Append(LogSeverity::kInfo, QStringLiteral("four"));

  ASSERT_EQ(model.rowCount(), 3);
  EXPECT_EQ(model.index(0).data(Qt::DisplayRole).toString(),
            QStringLiteral("two"));
  EXPECT_EQ(model.index(2).data(Qt::DisplayRole).toString(),
            QStringLiteral("four"));
}

TEST(LogMessageModelTest, CapacityStaysBoundedOverManyAppends) {
  LogMessageModel model(5);
  for (int i = 0; i < 100; ++i) {
    model.Append(LogSeverity::kInfo, QString::number(i));
  }
  ASSERT_EQ(model.rowCount(), 5);
  EXPECT_EQ(model.index(0).data(Qt::DisplayRole).toString(),
            QStringLiteral("95"));
  EXPECT_EQ(model.index(4).data(Qt::DisplayRole).toString(),
            QStringLiteral("99"));
}

TEST(LogMessageModelTest, SeverityForegroundColoursAreThemeAware) {
  LogMessageModel model;
  model.Append(LogSeverity::kError, QStringLiteral("boom"));
  model.Append(LogSeverity::kInfo, QStringLiteral("plain"));
  model.Append(LogSeverity::kDebug, QStringLiteral("detail"));

  model.SetDarkTheme(false);
  const QVariant light_error = model.index(0).data(Qt::ForegroundRole);
  ASSERT_TRUE(light_error.canConvert<QBrush>());

  model.SetDarkTheme(true);
  const QVariant dark_error = model.index(0).data(Qt::ForegroundRole);
  ASSERT_TRUE(dark_error.canConvert<QBrush>());
  EXPECT_NE(light_error.value<QBrush>().color(),
            dark_error.value<QBrush>().color());

  // Info uses the view's default text colour.
  EXPECT_FALSE(model.index(1).data(Qt::ForegroundRole).isValid());
  // Debug/trace are de-emphasised but still coloured.
  EXPECT_TRUE(model.index(2).data(Qt::ForegroundRole).canConvert<QBrush>());
}

TEST(LogMessageModelTest, ThemeChangeEmitsDataChangedForForeground) {
  LogMessageModel model;
  model.Append(LogSeverity::kError, QStringLiteral("boom"));

  int data_changed_count = 0;
  QObject::connect(&model, &QAbstractItemModel::dataChanged,
                   [&data_changed_count] { ++data_changed_count; });
  model.SetDarkTheme(true);
  EXPECT_EQ(data_changed_count, 1);
  // Re-applying the same theme is a no-op.
  model.SetDarkTheme(true);
  EXPECT_EQ(data_changed_count, 1);
}

TEST(LogMessageModelTest, ClearResetsTheModel) {
  LogMessageModel model;
  model.Append(LogSeverity::kInfo, QStringLiteral("one"));
  model.Append(LogSeverity::kInfo, QStringLiteral("two"));
  model.Clear();
  EXPECT_EQ(model.rowCount(), 0);
}

}  // namespace
}  // namespace videosynth::gui
