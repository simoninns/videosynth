/*
 * File:        main.cpp
 * Module:      main
 * Purpose:     Implements CLI entrypoint and runtime option parsing.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <iostream>
#include <string>

#include "videosynth/generation_stage.h"
#include "videosynth/logger.h"
#include "videosynth/output_stage.h"
#include "videosynth/pipeline.h"
#include "videosynth/project_validator.h"
#include "videosynth/yaml_project_parser.h"

namespace {

bool IsValidLogLevel(const std::string& log_level) {
  return log_level == "info" || log_level == "debug" || log_level == "trace";
}

videosynth::LogLevel ParseLogLevel(const std::string& log_level) {
  if (log_level == "debug") {
    return videosynth::LogLevel::kDebug;
  }
  if (log_level == "trace") {
    return videosynth::LogLevel::kTrace;
  }
  return videosynth::LogLevel::kInfo;
}

void PrintUsage() {
  std::cout << "Usage:\n"
            << "  videosynth --project <path> [options]\n"
            << "  videosynth --project <path> --validate [options]\n\n"
            << "Options:\n"
            << "  --project   Path to YAML project file (required).\n"
            << "  Output paths are read from project YAML under output.video_path and output.metadata_path.\n"
            << "  --validate  Validate only; do not generate output.\n"
            << "  --log-level <level>  Set log level: info, debug, or trace.\n"
            << "  --log-file <filename>  Write logs to a file as well as stderr.\n";
}

}  // namespace

int main(int argc, char** argv) {
  videosynth::RunOptions options;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];

    if (arg == "--project" && i + 1 < argc) {
      options.project_path = argv[++i];
    } else if (arg == "--validate") {
      options.validate_only = true;
    } else if (arg == "--log-level" && i + 1 < argc) {
      options.log_level = argv[++i];
    } else if (arg == "--log-file" && i + 1 < argc) {
      options.log_file = argv[++i];
    } else {
      PrintUsage();
      return 2;
    }
  }

  if (!IsValidLogLevel(options.log_level)) {
    PrintUsage();
    return 2;
  }

  if (options.project_path.empty()) {
    PrintUsage();
    return 2;
  }

  videosynth::SpdlogLogger logger(ParseLogLevel(options.log_level), options.log_file);
  logger.Debug("Logging configured at level '" + options.log_level + "'.");
  if (!options.log_file.empty()) {
    logger.Debug("Log file enabled: " + options.log_file);
  }

  videosynth::YamlProjectParser parser(&logger);
  videosynth::ProjectValidator validator(nullptr, &logger);
  videosynth::GenerationStage generation(&logger);
  videosynth::OutputStage output(&logger);

  videosynth::VideoSynthPipeline pipeline(&parser, &validator, &generation, &output, &logger);
  return pipeline.Run(options) ? 0 : 1;
}
