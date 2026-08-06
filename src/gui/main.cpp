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
#include <QString>
#include <QStringList>
#include <iostream>

#include "logging_options.h"
#include "main_window.h"
#include "qt_message_bridge.h"
#include "theme_controller.h"
#include "videosynth/logger.h"
#include "videosynth_version.h"

int main(int argc, char* argv[]) {
  // --log-level/--log-file must be known before QApplication exists so even
  // its construction messages honour them; QCommandLineParser cannot run
  // that early, so the raw argument list is scanned directly. The same exit
  // code (2) as the CLI signals a usage error.
  QStringList raw_arguments;
  raw_arguments.reserve(argc);
  for (int i = 0; i < argc; ++i) {
    raw_arguments.append(QString::fromLocal8Bit(argv[i]));
  }
  videosynth::gui::LoggingOptions logging_options;
  QString logging_error;
  if (!videosynth::gui::ParseLoggingOptions(raw_arguments, &logging_options,
                                            &logging_error)) {
    std::cerr << logging_error.toStdString() << "\n";
    return 2;
  }

  // Route Qt's own diagnostics through the application logger and drop the
  // known Wayland/XCB platform noise. Installed before QApplication so even
  // its construction messages are filtered. The logger is static so it
  // outlives any late message on the handler thread.
  static videosynth::SpdlogLogger qt_logger(
      videosynth::gui::ToLogLevel(logging_options.log_level),
      logging_options.log_file.toStdString());
  videosynth::gui::InstallQtMessageBridge(&qt_logger);
  if (!logging_options.log_level.isEmpty()) {
    qt_logger.Debug("Logging configured at level '" +
                    logging_options.log_level.toStdString() + "'.");
  }
  if (!logging_options.log_file.isEmpty()) {
    qt_logger.Debug("Log file enabled: " +
                    logging_options.log_file.toStdString());
  }

  QApplication::setHighDpiScaleFactorRoundingPolicy(
      Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

  QApplication app(argc, argv);

  // Resources live in the static GUI library, so registration is explicit.
  Q_INIT_RESOURCE(videosynth_gui_resources);

  QApplication::setOrganizationName(QStringLiteral("videosynth"));
  QApplication::setApplicationName(QStringLiteral("videosynth-gui"));
  QApplication::setApplicationVersion(QStringLiteral(VIDEOSYNTH_VERSION));
  // Must match the installed desktop entry's basename (see
  // packaging/linux/) so desktop shells associate the running window with its
  // menu entry and show the application icon and name.
  QApplication::setDesktopFileName(
      QStringLiteral("io.github.simoninns.VideoSynth"));
  QApplication::setWindowIcon(
      QIcon(QStringLiteral(":/videosynth-gui/icon.png")));

  QCommandLineParser parser;
  parser.setApplicationDescription(
      QStringLiteral("VideoSynth project authoring and generation GUI"));
  parser.addHelpOption();
  parser.addVersionOption();
  // Already applied above; registered here so --help documents them and
  // process() accepts them.
  parser.addOptions({
      {QStringLiteral("log-level"),
       QStringLiteral("Set log level: info, debug, or trace."),
       QStringLiteral("level")},
      {QStringLiteral("log-file"),
       QStringLiteral("Write logs to a file as well as stderr."),
       QStringLiteral("filename")},
  });
  parser.process(app);

  videosynth::gui::ThemeController theme_controller(&app);
  theme_controller.Initialize();

  videosynth::gui::MainWindow window(&theme_controller, logging_options);
  window.show();

  const int exit_code = QApplication::exec();
  // Detach before the logger is destroyed at process exit.
  videosynth::gui::InstallQtMessageBridge(nullptr);
  return exit_code;
}
