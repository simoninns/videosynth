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
            << "  videosynth --project <path> --output <path> --metadata <path> [options]\n"
            << "  videosynth --project <path> --validate [options]\n\n"
            << "Options:\n"
            << "  --project   Path to YAML project file (required).\n"
            << "  --output    Path to output CVBS video file (required unless --validate).\n"
            << "  --metadata  Path to metadata output file (required unless --validate).\n"
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
    } else if (arg == "--output" && i + 1 < argc) {
      options.output_path = argv[++i];
    } else if (arg == "--metadata" && i + 1 < argc) {
      options.metadata_path = argv[++i];
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

  if (!options.validate_only && (options.output_path.empty() || options.metadata_path.empty())) {
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
