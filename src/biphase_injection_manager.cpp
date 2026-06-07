/*
 * File:        biphase_injection_manager.cpp
 * Module:      biphase_injection_manager
 * Purpose:     Orchestrates biphase and 40-bit FM code injection into VBI
 *              lines of the video signal for LaserDisc authoring per
 *              IEC 60856 (PAL) and IEC 60857 (NTSC).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "videosynth/biphase_injection_manager.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

#include "videosynth/biphase_utils.h"
#include "videosynth/cav_code_generator.h"
#include "videosynth/clv_code_generator.h"
#include "videosynth/signal_shaping.h"

namespace videosynth {

namespace {

// PAL biphase: 30% of 700 mV white per IEC 60856.
constexpr double kPalBiphaseBaselineMv = 210.0;

// NTSC biphase: 0 IRE = 0 mV per IEC 60857.
constexpr double kNtscBiphaseBaselineMv = 0.0;

// 0.160 H horizontal start offset for NTSC white flag per IEC 60857 Figure 12.
constexpr double k160hFraction = 0.160;

// 0.172 H horizontal start offset fraction per IEC 60856 Figure 14 /
// IEC 60857 Figure 11.
constexpr double k172hFraction = 0.172;

// 0.215 H horizontal start offset for NTSC 40-bit FM codes per
// IEC 60857 Figure 13.
constexpr double k215hFraction = 0.215;

// 0.790 H white flag pulse length per IEC 60857 Figure 12.
constexpr double k790hFraction = 0.790;

double BiphaseBaselineMv(Standard standard) {
  return (standard == Standard::kPal) ? kPalBiphaseBaselineMv
                                      : kNtscBiphaseBaselineMv;
}

std::string Lowercase(std::string value) {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

}  // namespace

void BiphaseInjectionManager::Reset() {
  current_section_ = nullptr;
  generators_.clear();
  fm_picture_number_generator_.reset();
  fm_programme_time_generator_.reset();
  white_flag_tracker_.reset();
  placement_engine_.reset();
  biphase_encoder_.reset();
  fm_encoder_.reset();
  has_laserdisc_ = false;
  disc_type_ = DiscType::kUnknown;
  section_type_ = SectionType::kUnknown;
}

bool BiphaseInjectionManager::ProcessFrame(
    std::vector<SampleFixed>* out_y_mv, int frame_sample_base,
    const std::vector<int>& line_sample_offsets,
    const std::vector<int>& line_sample_counts, const Section& section,
    Standard standard, double sample_rate_hz,
    const std::vector<LineTimingPrimitive>& frame_lines,
    int active_window_start_samples, int active_window_end_samples,
    std::vector<std::string>* errors) {
  if (out_y_mv == nullptr || errors == nullptr) {
    return false;
  }

  if (&section != current_section_) {
    if (!InitializeSection(section, standard, sample_rate_hz, errors)) {
      return false;
    }
    current_section_ = &section;
  }

  if (!has_laserdisc_) {
    return true;
  }

  const SignalLevels levels = GetSignalLevels(standard);
  const TimingConstants timing = GetTimingConstants(standard);

  // Per IEC 60857 Figure 12: NTSC white flag starts at 0.160 H.
  const int offset_160h_samples = static_cast<int>(
      std::round(k160hFraction * timing.samples_per_line_4fsc));

  // Per IEC 60856 Figure 14 / IEC 60857 Figure 11: programme_status and NTSC
  // clv_code start at 0.172 H from the start of the line.
  const int offset_172h_samples = static_cast<int>(
      std::round(k172hFraction * timing.samples_per_line_4fsc));

  // Per IEC 60857 Figure 13: NTSC 40-bit FM codes start at 0.215 H.
  const int offset_215h_samples = static_cast<int>(
      std::round(k215hFraction * timing.samples_per_line_4fsc));

  // Per IEC 60857 Figure 12: NTSC white flag pulse length = 0.790 H.
  const int flag_length_samples = static_cast<int>(
      std::round(k790hFraction * timing.samples_per_line_4fsc));

  for (const LineTimingPrimitive& line : frame_lines) {
    const int line_num = line.line_number_1based;

    if (!LinePlacementEngine::IsInBiphaseReservedRange(standard, line_num)) {
      continue;
    }

    const bool field_one = LinePlacementEngine::IsFieldOne(standard, line_num);
    const LineCodeAssignment assignment =
        placement_engine_->GetAssignment(line_num, field_one);

    if (!assignment.assigned) {
      continue;
    }

    const int line_index = line_num - 1;
    const int line_base =
        frame_sample_base +
        line_sample_offsets[static_cast<std::size_t>(line_index)];
    const int line_samples =
        line_sample_counts[static_cast<std::size_t>(line_index)];

    // Cap the active end at the line boundary to prevent out-of-bounds writes.
    const int active_end =
        std::min(active_window_end_samples, line_samples);

    const int start_sample =
        assignment.is_white_flag
            ? offset_160h_samples
            : (assignment.uses_172h_offset
                   ? offset_172h_samples
                   : (assignment.is_fm ? offset_215h_samples
                                       : active_window_start_samples));

    if (assignment.is_white_flag) {
      InjectWhiteFlag(out_y_mv, line_base, active_end, levels, start_sample,
                      flag_length_samples);
    } else if (assignment.is_fm) {
      InjectFmCode(out_y_mv, line_base, active_end, assignment.code_type,
                   field_one, levels, start_sample);
    } else {
      InjectBiphaseCode(out_y_mv, line_base, active_end, assignment.code_type,
                        standard, levels, start_sample);
    }
  }

  AdvanceGenerators();
  return true;
}

bool BiphaseInjectionManager::InitializeSection(
    const Section& section, Standard standard, double sample_rate_hz,
    std::vector<std::string>* errors) {
  generators_.clear();
  fm_picture_number_generator_.reset();
  fm_programme_time_generator_.reset();
  white_flag_tracker_.reset();
  placement_engine_.reset();
  biphase_encoder_.reset();
  fm_encoder_.reset();
  has_laserdisc_ = false;
  disc_type_ = DiscType::kUnknown;
  section_type_ = section.section_type;

  const Section::LineInjection* injection = nullptr;
  for (const Section::LineInjection& inj : section.line_injections) {
    if (Lowercase(inj.type) == "laserdisc") {
      injection = &inj;
      break;
    }
  }

  if (injection == nullptr) {
    return true;
  }

  disc_type_ = DiscTypeFromString(injection->disc_type);
  if (disc_type_ == DiscType::kUnknown) {
    errors->push_back("Biphase injection: unknown disc_type '" +
                      injection->disc_type + "'.");
    return false;
  }

  has_laserdisc_ = true;
  biphase_encoder_ = std::make_unique<BiphaseEncoder>(sample_rate_hz);
  if (standard == Standard::kNtsc) {
    fm_encoder_ = std::make_unique<FmEncoder>(sample_rate_hz);
  }

  std::vector<std::string> codes_present;

  for (const Section::LineInjectionCode& code : injection->codes) {
    codes_present.push_back(code.code_type);

    if (code.code_type == "lead_in") {
      generators_["lead_in"] = std::make_unique<LeadInCodeGenerator>();

    } else if (code.code_type == "lead_out") {
      generators_["lead_out"] = std::make_unique<LeadOutCodeGenerator>();

    } else if (code.code_type == "picture_number") {
      const int start = code.start_value_specified ? code.start_value : 1;
      generators_["picture_number"] =
          std::make_unique<CavPictureNumberGenerator>(start, standard);

    } else if (code.code_type == "picture_stop") {
      generators_["picture_stop"] =
          std::make_unique<PictureStopCodeGenerator>();

    } else if (code.code_type == "chapter_number") {
      generators_["chapter_number"] =
          std::make_unique<ChapterNumberGenerator>(code.chapter);

    } else if (code.code_type == "programme_status") {
      if (code.programme_status_specified) {
        const auto maybe_val = ParseBiphaseHexCode(code.programme_status);
        if (!maybe_val) {
          errors->push_back(
              "Biphase injection: invalid programme_status hex '" +
              code.programme_status + "'.");
          return false;
        }
        generators_["programme_status"] =
            std::make_unique<ProgrammeStatusCodeGenerator>(*maybe_val);
      }

    } else if (code.code_type == "users_code") {
      if (code.users_code_specified) {
        const auto maybe_val = ParseBiphaseHexCode(code.users_code);
        if (!maybe_val) {
          errors->push_back("Biphase injection: invalid users_code hex '" +
                            code.users_code + "'.");
          return false;
        }
        generators_["users_code"] =
            std::make_unique<UsersCodeGenerator>(*maybe_val);
      }

    } else if (code.code_type == "clv_code") {
      generators_["clv_code"] = std::make_unique<ClvCodeGenerator>();

    } else if (code.code_type == "programme_time_code") {
      generators_["programme_time_code"] =
          std::make_unique<ProgrammeTimeCodeGenerator>(0, 0, standard);

    } else if (code.code_type == "clv_picture_number") {
      generators_["clv_picture_number"] =
          std::make_unique<ClvPictureNumberGenerator>(standard);

    } else if (code.code_type == "fm_picture_number") {
      const int start = code.start_value_specified ? code.start_value : 1;
      fm_picture_number_generator_ =
          std::make_unique<FmPictureNumberGenerator>(start);

    } else if (code.code_type == "fm_programme_time") {
      fm_programme_time_generator_ =
          std::make_unique<FmProgrammeTimeGenerator>();

    } else if (code.code_type == "fm_white_flag") {
      white_flag_tracker_ = std::make_unique<WhiteFlagTracker>();
    }
  }

  placement_engine_ = std::make_unique<LinePlacementEngine>(
      standard, disc_type_, section_type_, codes_present);

  return true;
}

void BiphaseInjectionManager::InjectBiphaseCode(
    std::vector<SampleFixed>* out_y_mv, int line_base, int active_end,
    const std::string& code_type, Standard standard, const SignalLevels& levels,
    int start_sample) {
  CodeGenerator* gen = GetGenerator(code_type);
  if (gen == nullptr || biphase_encoder_ == nullptr) {
    return;
  }

  const uint32_t code_value = gen->CurrentCode();
  const double baseline_mv = BiphaseBaselineMv(standard);
  const double peak_mv = levels.white_mv;

  const auto waveform =
      biphase_encoder_->Generate24BitCode(code_value, baseline_mv, peak_mv);
  const int waveform_count = static_cast<int>(waveform.size());

  const SampleFixed baseline_fixed = MillivoltsToSampleFixed(baseline_mv);

  // Overwrite the VBI data region with the biphase baseline, then overlay the
  // waveform. Stops at active_end to avoid writing into the front porch.
  for (int i = start_sample; i < active_end; ++i) {
    (*out_y_mv)[static_cast<std::size_t>(line_base + i)] = baseline_fixed;
  }
  for (int i = 0; i < waveform_count && (start_sample + i) < active_end; ++i) {
    (*out_y_mv)[static_cast<std::size_t>(line_base + start_sample + i)] =
        waveform[static_cast<std::size_t>(i)];
  }
}

void BiphaseInjectionManager::InjectFmCode(std::vector<SampleFixed>* out_y_mv,
                                           int line_base, int active_end,
                                           const std::string& code_type,
                                           bool field_one,
                                           const SignalLevels& levels,
                                           int start_sample) {
  if (fm_encoder_ == nullptr) {
    return;
  }

  // 40-bit FM uses 0 IRE baseline regardless of standard (NTSC only).
  const double baseline_mv = kNtscBiphaseBaselineMv;
  const double peak_mv = levels.white_mv;

  FmData fm_data{};
  fm_data.field_one = field_one;

  if (code_type == "fm_picture_number" && fm_picture_number_generator_) {
    fm_data =
        fm_picture_number_generator_->CurrentData(field_one, section_type_);
  } else if (code_type == "fm_programme_time" && fm_programme_time_generator_) {
    fm_data =
        fm_programme_time_generator_->CurrentData(field_one, section_type_);
  } else {
    return;
  }

  const auto waveform =
      fm_encoder_->Generate40BitWaveform(fm_data, baseline_mv, peak_mv);
  const int waveform_count = static_cast<int>(waveform.size());

  const SampleFixed baseline_fixed = MillivoltsToSampleFixed(baseline_mv);

  for (int i = start_sample; i < active_end; ++i) {
    (*out_y_mv)[static_cast<std::size_t>(line_base + i)] = baseline_fixed;
  }
  for (int i = 0; i < waveform_count && (start_sample + i) < active_end; ++i) {
    (*out_y_mv)[static_cast<std::size_t>(line_base + start_sample + i)] =
        waveform[static_cast<std::size_t>(i)];
  }
}

void BiphaseInjectionManager::InjectWhiteFlag(
    std::vector<SampleFixed>* out_y_mv, int line_base, int active_end,
    const SignalLevels& levels, int start_sample, int flag_length_samples) {
  const double baseline_mv = levels.blanking_mv;
  const double peak_mv = levels.white_mv;
  const SampleFixed baseline_fixed = MillivoltsToSampleFixed(baseline_mv);

  // Clear the region from start_sample to active_end with blanking.
  for (int i = start_sample; i < active_end; ++i) {
    (*out_y_mv)[static_cast<std::size_t>(line_base + i)] = baseline_fixed;
  }

  // Apply shaped white flag pulse (135 ns rise/fall per IEC 60857 Figure 12).
  const int ramp = fm_encoder_ ? fm_encoder_->ramp_samples() : 4;
  const int flag_end = std::min(start_sample + flag_length_samples, active_end);
  for (int i = start_sample; i < flag_end; ++i) {
    const int rel = i - start_sample;
    const double level = ShapedPulseLevel(rel, flag_length_samples, ramp,
                                          baseline_mv, peak_mv);
    (*out_y_mv)[static_cast<std::size_t>(line_base + i)] =
        MillivoltsToSampleFixed(level);
  }
}

void BiphaseInjectionManager::AdvanceGenerators() {
  for (auto& [code_type, gen] : generators_) {
    gen->Advance();
  }
  if (fm_picture_number_generator_) {
    fm_picture_number_generator_->Advance(section_type_);
  }
  if (fm_programme_time_generator_) {
    fm_programme_time_generator_->Advance(section_type_);
  }
}

CodeGenerator* BiphaseInjectionManager::GetGenerator(
    const std::string& code_type) const {
  const auto it = generators_.find(code_type);
  if (it == generators_.end()) {
    return nullptr;
  }
  return it->second.get();
}

}  // namespace videosynth
