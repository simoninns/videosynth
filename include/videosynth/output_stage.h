/*
 * File:        output_stage.h
 * Module:      output_stage
 * Purpose:     Writes generated sample buffers to output and metadata files.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include "videosynth/interfaces.h"

namespace videosynth {

class OutputStage final : public IOutputStage {
 public:
    explicit OutputStage(ILogger* logger = nullptr);

  bool Write(const Project& project,
             const std::vector<double>& y_mv,
             const std::vector<double>& c_mv,
             std::vector<std::string>* errors) override;

 private:
    ILogger* logger_;
};

}  // namespace videosynth
