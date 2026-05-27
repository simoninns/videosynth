#include "videosynth/logger.h"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace videosynth {

SpdlogLogger::SpdlogLogger(bool verbose) {
  logger_ = spdlog::stderr_color_mt("videosynth");
  logger_->set_level(verbose ? spdlog::level::debug : spdlog::level::info);
  logger_->set_pattern("[%^%l%$] %v");
}

void SpdlogLogger::Info(const std::string& message) { logger_->info(message); }

void SpdlogLogger::Error(const std::string& message) { logger_->error(message); }

void SpdlogLogger::Debug(const std::string& message) { logger_->debug(message); }

}  // namespace videosynth
