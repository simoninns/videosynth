#pragma once

#include <string>
#include <vector>

#include "videosynth/model.h"
#include "videosynth/results.h"

namespace videosynth {

struct RunOptions {
  std::string project_path;
  std::string output_path;
  std::string metadata_path;
  bool validate_only = false;
  bool verbose = false;
};

class ILogger {
 public:
  virtual ~ILogger() = default;
  virtual void Info(const std::string& message) = 0;
  virtual void Error(const std::string& message) = 0;
  virtual void Debug(const std::string& message) = 0;
};

class IProjectParser {
 public:
  virtual ~IProjectParser() = default;
  virtual ParseResult ParseFile(const std::string& path) = 0;
};

class IProjectValidator {
 public:
  virtual ~IProjectValidator() = default;
  virtual ValidationResult Validate(const Project& project) = 0;
};

class IGenerationStage {
 public:
  virtual ~IGenerationStage() = default;
  virtual bool Generate(const Project& project,
                        std::vector<double>* out_y_mv,
                        std::vector<double>* out_c_mv,
                        std::vector<std::string>* errors) = 0;
};

class IOutputStage {
 public:
  virtual ~IOutputStage() = default;
  virtual bool Write(const Project& project,
                     const std::vector<double>& y_mv,
                     const std::vector<double>& c_mv,
                     const std::string& output_path,
                     const std::string& metadata_path,
                     std::vector<std::string>* errors) = 0;
};

}  // namespace videosynth
