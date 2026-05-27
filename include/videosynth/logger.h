#pragma once

#include <string>

#include <memory>

#include <spdlog/logger.h>

#include "videosynth/interfaces.h"

namespace videosynth {

class SpdlogLogger final : public ILogger {
 public:
  explicit SpdlogLogger(bool verbose);

  void Info(const std::string& message) override;
  void Error(const std::string& message) override;
  void Debug(const std::string& message) override;

 private:
  std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace videosynth
