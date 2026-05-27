#pragma once

#include <string>

#include "videosynth/interfaces.h"

namespace videosynth {

class YamlProjectParser final : public IProjectParser {
 public:
  ParseResult ParseFile(const std::string& path) override;
};

}  // namespace videosynth
