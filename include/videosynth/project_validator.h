#pragma once

#include "videosynth/interfaces.h"

namespace videosynth {

class ProjectValidator final : public IProjectValidator {
 public:
  ValidationResult Validate(const Project& project) override;
};

}  // namespace videosynth
