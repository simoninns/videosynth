/*
 * File:        generation_stage.h
 * Module:      generation_stage
 * Purpose:     Generates CVBS-domain Y and C sample buffers from frame-based source data.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include "videosynth/interfaces.h"

namespace videosynth {

class GenerationStage final : public IGenerationStage {
 public:
    explicit GenerationStage(ILogger* logger = nullptr);

  bool Generate(const Project& project,
                std::vector<SampleFixed>* out_y_mv,
                std::vector<SampleFixed>* out_c_mv,
                std::vector<std::string>* errors) override;

 private:
    ILogger* logger_;
};

}  // namespace videosynth
