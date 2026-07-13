/*
 * File:        main.cpp
 * Module:      gui
 * Purpose:     GUI application entry point
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>

#include "main_window.h"
#include "qt_message_bridge.h"
#include "theme_controller.h"
#include "videosynth/logger.h"
#include "videosynth_version.h"

int main(int argc, char* argv[]) {
  // Route Qt's own diagnostics through the application logger and drop the
  // known Wayland/XCB platform noise. Installed before QApplication so even
  // its construction messages are filtered. The logger is static so it
  // outlives any late message on the handler thread.
  static videosynth::SpdlogLogger qt_logger(videosynth::LogLevel::kInfo,
                                            /*log_file=*/"");
  videosynth::gui::InstallQtMessageBridge(&qt_logger);

  QApplication::setHighDpiScaleFactorRoundingPolicy(
      Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

  QApplication app(argc, argv);

  // Resources live in the static GUI library, so registration is explicit.
  Q_INIT_RESOURCE(videosynth_gui_resources);

  QApplication::setOrganizationName(QStringLiteral("videosynth"));
  QApplication::setApplicationName(QStringLiteral("videosynth-gui"));
  QApplication::setApplicationVersion(QStringLiteral(VIDEOSYNTH_VERSION));
  QApplication::setDesktopFileName(QStringLiteral("videosynth-gui"));
  QApplication::setWindowIcon(
      QIcon(QStringLiteral(":/videosynth-gui/icon.png")));

  QCommandLineParser parser;
  parser.setApplicationDescription(
      QStringLiteral("VideoSynth project authoring and generation GUI"));
  parser.addHelpOption();
  parser.addVersionOption();
  parser.process(app);

  videosynth::gui::ThemeController theme_controller(&app);
  theme_controller.Initialize();

  videosynth::gui::MainWindow window(&theme_controller);
  window.show();

  const int exit_code = QApplication::exec();
  // Detach before the logger is destroyed at process exit.
  videosynth::gui::InstallQtMessageBridge(nullptr);
  return exit_code;
}
