#pragma once

#include "videosynth/interfaces.h"

namespace videosynth {

class OutputStage final : public IOutputStage {
 public:
  bool Write(const Project& project,
             const std::vector<double>& y_mv,
             const std::vector<double>& c_mv,
             const std::string& output_path,
             const std::string& metadata_path,
             std::vector<std::string>* errors) override;
};

}  // namespace videosynth
