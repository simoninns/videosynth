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

void PrintUsage() {
  std::cout << "Usage:\n"
            << "  videosynth --project <path> [options]\n"
            << "  videosynth --project <path> --validate [options]\n\n"
            << "Options:\n"
            << "  --project   Path to YAML project file (required).\n"
            << "  Output paths are read from project YAML under output.video_path and output.metadata_path.\n"
            << "  --validate  Validate only; do not generate output.\n"
            << "  --verbose   Enable debug logging.\n";
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
    } else if (arg == "--verbose") {
      options.verbose = true;
    } else {
      PrintUsage();
      return 2;
    }
  }

  if (options.project_path.empty()) {
    PrintUsage();
    return 2;
  }

  videosynth::YamlProjectParser parser;
  videosynth::ProjectValidator validator;
  videosynth::GenerationStage generation;
  videosynth::OutputStage output;
  videosynth::SpdlogLogger logger(options.verbose);

  videosynth::VideoSynthPipeline pipeline(&parser, &validator, &generation, &output, &logger);
  return pipeline.Run(options) ? 0 : 1;
}
