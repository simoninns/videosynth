/*
 * File:        gui_test_main.cpp
 * Module:      gui_tests
 * Purpose:     GoogleTest entry point providing a QCoreApplication fixture
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <QCoreApplication>

// A QCoreApplication (not QApplication: no display needed) gives QObject
// tests a thread dispatcher so queued connections and QTimer events can be
// delivered by pumping processEvents; tests stay deterministic and headless.
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("videosynth-tests"));
  QCoreApplication::setApplicationName(QStringLiteral("videosynth-gui-tests"));

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
