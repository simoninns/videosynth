#pragma once

#include <string>
#include <vector>

#include "videosynth/model.h"

namespace videosynth {

struct ParseResult {
  bool ok = false;
  Project project;
  std::vector<std::string> errors;
};

struct ValidationResult {
  bool is_valid = false;
  std::vector<std::string> errors;
};

}  // namespace videosynth
