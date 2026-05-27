#pragma once

#include "videosynth/interfaces.h"

namespace videosynth {

class VideoSynthPipeline {
 public:
  VideoSynthPipeline(IProjectParser* parser,
                     IProjectValidator* validator,
                     IGenerationStage* generation,
                     IOutputStage* output,
                     ILogger* logger);

  bool Run(const RunOptions& options);

 private:
  IProjectParser* parser_;
  IProjectValidator* validator_;
  IGenerationStage* generation_;
  IOutputStage* output_;
  ILogger* logger_;
};

}  // namespace videosynth
