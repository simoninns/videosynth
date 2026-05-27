#pragma once

#include "videosynth/interfaces.h"

namespace videosynth {

class GenerationStage final : public IGenerationStage {
 public:
  bool Generate(const Project& project,
                std::vector<double>* out_y_mv,
                std::vector<double>* out_c_mv,
                std::vector<std::string>* errors) override;
};

}  // namespace videosynth
