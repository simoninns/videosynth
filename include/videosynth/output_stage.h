/*
 * File:        output_stage.h
 * Module:      output_stage
 * Purpose:     Writes generated sample buffers to output and metadata files.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <fstream>

#include "videosynth/interfaces.h"

namespace videosynth {

class OutputStage final : public IOutputStage {
 public:
  explicit OutputStage(ILogger* logger = nullptr);

  bool BeginWrite(const Project& project, std::size_t expected_frame_count,
                  std::vector<std::string>* errors) override;

  bool AppendSamples(const std::vector<SampleFixed>& y_mv,
                     const std::vector<SampleFixed>& c_mv,
                     std::vector<std::string>* errors) override;

  bool FinalizeWrite(std::vector<std::string>* errors) override;

  bool Write(const Project& project, const std::vector<SampleFixed>& y_mv,
             const std::vector<SampleFixed>& c_mv,
             std::vector<std::string>* errors) override;

 private:
  bool IsSessionOpen() const;

  ILogger* logger_;
  bool write_session_open_ = false;
  Project current_project_;
  std::ofstream video_stream_;
  std::size_t expected_frame_count_ = 0;
  std::size_t written_samples_ = 0;
  std::size_t input_frame_span_ = 0;
  std::size_t output_frame_span_ = 0;
  bool has_nonstandard_ = false;
};

}  // namespace videosynth
